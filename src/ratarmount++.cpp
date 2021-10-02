#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cxxopts.hpp>
#include <indexed_bzip2/common.hpp>
#include <SQLiteCpp/SQLiteCpp.h>

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <errno.h>


#if 0
CREATE TABLE IF NOT EXISTS "files" (
    "path"          VARCHAR(65535) NOT NULL,  /* path with leading and without trailing slash */
    "name"          VARCHAR(65535) NOT NULL,
    "offsetheader"  INTEGER,  /* seek offset from TAR file where these file's contents resides */
    "offset"        INTEGER,  /* seek offset from TAR file where these file's contents resides */
    "size"          INTEGER,
    "mtime"         INTEGER,
    "mode"          INTEGER,
    "type"          INTEGER,
    "linkname"      VARCHAR(65535),
    "uid"           INTEGER,
    "gid"           INTEGER,
    /* True for valid TAR files. Internally used to determine where to mount recursive TAR files. */
    "istar"         BOOL   ,
    "issparse"      BOOL   ,  /* for sparse files the file size refers to the expanded size! */
    /* See SQL benchmarks for decision on the primary key.
     * See also https://www.sqlite.org/optoverview.html
     * (path,name) tuples might appear multiple times in a TAR if it got updated.
     * In order to also be able to show older versions, we need to add
     * the offsetheader column to the primary key. */
    PRIMARY KEY (path,name,offsetheader)
);
#endif


class FuseMount
{
public:
    FuseMount(
        const std::vector<std::string>& pathsToMount,
        const std::string&              mountPoint,
        unsigned int                    debugLevel = 1
    ) :
        m_db( pathsToMount.front() + ".index.sqlite" ),
        m_debugLevel( debugLevel )
    {
        setInstance( std::unique_ptr<FuseMount>( this ) );
    }

    static void*
    init(
        struct fuse_conn_info* /* connection */,
        struct fuse_config*    /* fuseConfig */
    )
    {
        return nullptr;
    }

    static int
    getattr(
        const char*  path,
        struct stat* stat
#if FUSE_USE_VERSION >= 30
        ,struct fuse_file_info* /* fileInfo */
#endif
    )
    {
        if ( instance()->m_debugLevel >= 3 ) {
            std::cerr << "[getattr(path=" << path << ")]\n";
        }

        std::memset(stat, 0, sizeof(struct stat));

        if ( path == std::string("/") ) {
            stat->st_mode = S_IFDIR | 0755;
            stat->st_nlink = 2;
            return 0;
        }

        const int fileVersion = 0;

        std::string statement;
        statement = R"(
            SELECT size, mtime, mode, uid, gid, linkname IS NOT NULL AND linkname != "" FROM "files"
                WHERE "path" == (?) AND "name" == (?)
                ORDER BY "offsetheader"
            )";
        statement += fileVersion > 0 ? "ASC" : "DESC";
        statement += " LIMIT 1 OFFSET (?)";

        SQLite::Statement query( instance()->m_db, statement );
        ++instance()->mn_queryCount;

        const std::string_view sPath( path );
        if ( sPath.empty() || ( sPath.front() != '/' ) ) {
            return EINVAL;
        }

        /* The above check guarantees at least one slash. */
        const auto lastSlash = sPath.rfind( '/' );
        assert( lastSlash < sPath.size() );

        const std::string parentPath( path, lastSlash );
        const std::string fileName( path + lastSlash + 1, sPath.size() - ( lastSlash + 1 ) );

        query.bindNoCopy( 1, parentPath );
        query.bindNoCopy( 2, fileName );
        query.bind( 3, fileVersion > 0 ? fileVersion - 1 : -fileVersion );

        const auto gotResult = query.executeStep();
        if ( !gotResult ) {
            return -ENOENT;
        }

        /* https://pubs.opengroup.org/onlinepubs/007908799/xsh/sysstat.h.html
         * https://pubs.opengroup.org/onlinepubs/007908799/xsh/systypes.h.html */
        stat->st_size  = query.getColumn( 0 );
        stat->st_mtime = query.getColumn( 1 );
        stat->st_mode  = query.getColumn( 2 );
        stat->st_uid   = query.getColumn( 3 );
        stat->st_gid   = query.getColumn( 4 );

        if ( static_cast<int>( query.getColumn( 5 ) ) != 0 ) {
            stat->st_mode |= S_IFLNK;
        }
        stat->st_nlink = 1;

        return 0;
    }

    static int
    readdir(
        const char*             path,
        void*                   buf,
        fuse_fill_dir_t         filler,
        off_t                   /* offset */,
        struct fuse_file_info*  /* fileInfo */
#if FUSE_USE_VERSION >= 30
        ,enum fuse_readdir_flags flags
#endif
    )
    {
        const auto addEntry = [=]( const char* name )
            {
                filler(
                    buf,
                    name,
                    nullptr,
                    0
                    #if FUSE_USE_VERSION >= 30
                    ,0
                    #endif
                );
            };

        SQLite::Statement query(
            instance()->m_db,
            R"(SELECT DISTINCT name FROM "files" WHERE "path" == (?) AND "name" is NOT NULL and "name" != "")"
        );
        ++instance()->mn_queryCount;

        const std::string_view sPath( path );
        if ( sPath.empty() || ( sPath.front() != '/' ) ) {
            std::cerr << "Path argument should start with '/'!\n";
            return EINVAL;
        }

        const auto lastNonSlash = sPath.find_last_not_of( '/' );
        const std::string cleanedPath( sPath.data(), lastNonSlash < sPath.size() ? lastNonSlash + 1 : 0 );

        query.bindNoCopy( 1, cleanedPath );

        bool gotResults = false;
        while ( query.executeStep() ) {
            addEntry( static_cast<std::string>( query.getColumn( 0 ) ).c_str() );
            gotResults = true;
        }

        addEntry(".");
        addEntry("..");

        return 0;
    }

    static int
    open(
        const char*            path,
        struct fuse_file_info* fileInfo
    )
    {
        if ( "/" + instance()->m_filename != path ) {
            return -ENOENT;
        }

        if ( ( fileInfo->flags & O_ACCMODE ) != O_RDONLY ) {
            return -EACCES;
        }

        return 0;
    }

    static int
    read(
        const char*            path,
        char*                  buf,
        size_t                 size,
        off_t                  offset,
        struct fuse_file_info* /* fileInfo */
    )
    {
        if ( "/" + instance()->m_filename != path ) {
            return -ENOENT;
        }

        const auto len = instance()->m_contents.size();
        if ( ( offset >= 0 ) && ( static_cast<size_t>( offset ) < len ) ) {
            if (offset + size > len) {
                size = len - offset;
            }
            std::memcpy(buf, instance()->m_contents.data() + offset, size);
        } else {
            size = 0;
        }

        return size;
    }

    [[nodiscard]] static FuseMount*
    instance()
    {
        return m_instance.get();
    }

    void
    setInstance( std::unique_ptr<FuseMount> instance )
    {
        m_instance = std::move( instance );
    }

private:
    static std::unique_ptr<FuseMount> m_instance;

    SQLite::Database m_db;
public:
    std::atomic<size_t> mn_queryCount{ 0 };
private:
    const unsigned int m_debugLevel;

    const std::string m_filename = "foo";
    const std::string m_contents = "hello";
};


std::unique_ptr<FuseMount> FuseMount::m_instance;


void
printHelp( const cxxopts::Options& options )
{
    std::cout
    << options.help()
    << "\n"
    << "Examples:\n"
    << "\n"
    << "Mount a TAR with pre-existing .sqlite.index:\n"
    << "\n"
    << "  ratarmount++ file.tar\n"
    << std::endl;

    /** @todo Replace "positional parameters" with "mount_source [mount_source ...] [mount_point]" */
}


std::string
getFilePath( cxxopts::ParseResult const& parsedArgs,
             std::string          const& argument )
{
    if ( parsedArgs.count( argument ) > 0 ) {
        auto path = parsedArgs[argument].as<std::string>();
        if ( path != "-" ) {
            return path;
        }
    }
    return {};
}


int
cli( int argc, char** argv )
{
    /**
     * @note For some reason implicit values do not mix very well with positional parameters!
     *       Parameters given to arguments with implicit values will be matched by the positional argument instead!
     */
    cxxopts::Options options( "ratarmount++",
                              "A reimplementation of ratarmount using C++ for lower access latencies." );

    options.add_options( "Input" )
        ( "mount_source", "TAR archive to mount.",
          cxxopts::value<std::vector<std::string> >() );

    options.add_options( "Output" )
        ( "h,help", "Print this help mesage." )
        ( "d,debug", "A higher debug level means more information being printed out. Use -d 0 to turn to quieten it.",
          cxxopts::value<unsigned int>()->default_value( "1" ) )
        ( "V,version", "Display software version." );

    options.add_options( "Advanced" )
        ( "f,foreground",
          "Keeps the python program in foreground so it can print debug output when the mounted path is accessed." );

    options.parse_positional( { "mount_source" } );

    /* cxxopts allows to specifiy arguments multiple times. But if the argument type is not a vector, then only
     * the last value will be kept! Therefore, do not check against this usage and simply use that value.
     * Arguments may only be queried with as if they have (default) values. */

    const auto parsedArgs = options.parse( argc, argv );

    const auto debugLevel = parsedArgs["debug"].as<unsigned int>();

    /* Check against simple commands like help and version. */

    if ( parsedArgs.count( "help" ) > 0 ) {
        printHelp( options );
        return 0;
    }

    if ( parsedArgs.count( "version" ) > 0 ) {
        std::cout << "ratarmount++ 0.0.1\n";
        return 0;
    }

    /* Parse input and output file specifications. */

    std::vector<std::string> positionals;
    if ( parsedArgs.count( "mount_source" ) > 0 ) {
        positionals = parsedArgs["mount_source"].as<std::vector<std::string> >();
    }

    if ( positionals.empty() ) {
        std::cerr << "No suitable arguments were given. Please refer to the help!\n\n";
        printHelp( options );
        return 1;
    }

#ifndef NDEBUG
    SQLite::Database db( positionals.front() + ".index.sqlite" );

    const auto t0 = now();

    SQLite::Statement query(db, "SELECT * FROM files WHERE name == ?");
    // Bind the integer value 6 to the first parameter of the SQL query
    query.bind(1, "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000008266");

    // Loop to execute the query step by step, to get rows of result
    while (query.executeStep())
    {
        // Demonstrate how to get some typed column value
        std::string path = query.getColumn(0);
        std::string name = query.getColumn(1);
        /* SQLiteCpp has no unsigned int conversion operators! */
         const auto offset    = static_cast<int64_t>( query.getColumn(3) );

        std::cout << "Found requested file at offset: " << offset << " in path: " << path << std::endl;
        break;
    }

    const auto t1 = now();
    std::cout << "Queried SQLite datbase in " << duration( t0, t1 ) << "s\n";
#endif

    // Initialize fuse
    fuse_args args = FUSE_ARGS_INIT(0, nullptr);
    fuse_opt_add_arg(&args, argv[0]);
    //fuse_opt_add_arg( &args, "-odebug" );
    if ( parsedArgs.count( "foreground" ) > 0 ) {
        fuse_opt_add_arg( &args, "-f" );
    }
    /**
     * time find mountPoint > /dev/null results:
     *  - ratarmount++ multi-threaded   12.1s
     *  - ratarmount++ -s               7.9s
     *  - ratarmount single-threaded    4.2s
     * @todo Why is it slower than the Python version?!
     */
    fuse_opt_add_arg( &args, "-s" );  // activates single-threaded mode. By default it is multi-threaded!
    fuse_opt_add_arg( &args, "mountPoint" );

    auto* fuseMount = new FuseMount( positionals, positionals.back(), debugLevel );

    /* Must be static or FUSE will segfault weirdly. */
    static fuse_operations fuseOperations;
    fuseOperations.getattr = &FuseMount::getattr;
    fuseOperations.readdir = &FuseMount::readdir;
    fuseOperations.open    = &FuseMount::open;
    fuseOperations.read    = &FuseMount::read;

    std::cerr << "Starting FUSE!\n";
	const auto returnCode = fuse_main(args.argc, args.argv, &fuseOperations, fuseMount);
    std::cerr << "Finished FUSE!\n";

    std::cerr << "Used " << fuseMount->mn_queryCount << " SQLite queries!\n";

    return returnCode;
}


int
main( int argc, char** argv )
{
    try
    {
        return cli( argc, argv );
    }
    catch ( const std::exception& exception )
    {
        std::cerr << "Caught exception:\n" << exception.what() << "\n";
        return 1;
    }

    return 1;
}
