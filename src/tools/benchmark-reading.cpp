
#include <filesystem>
#include <list>
#include <mutex>
#include <string>
#include <vector>

#include <sys/stat.h>

#include <indexed_bzip2/common.hpp>
#include <indexed_bzip2/ThreadPool.hpp>


uint64_t
getFileSize( const std::string& filePath )
{
    struct stat statData;
    const auto rc = stat( filePath.c_str(), &statData );
    if ( ( rc != 0 ) || ( statData.st_size < 0 ) ) {
        throw std::runtime_error( "Could not get file size!" );
    }
    return static_cast<uint64_t>( statData.st_size );
}


class FolderReader
{
public:
    FolderReader( std::string folderToRead ) :
        m_folderToRead( std::move( folderToRead ) )
    {}

    void
    loadAll()
    {
        std::list<std::future<void> > futures;

        for ( auto it = std::filesystem::recursive_directory_iterator( m_folderToRead );
              it != std::filesystem::recursive_directory_iterator();
              ++it )
        {
            if ( it->is_regular_file() ) {
                futures.emplace_back( m_threadPool.submitTask( std::bind( &FolderReader::loadFile, this, it->path() ) ) );
            }
        }

        for ( auto& future : futures ) {
            future.get();
        }
    }

    void
    loadFile( const std::string& path )
    {
        const auto fileSize = getFileSize( path );
        std::vector<char> fileContents( fileSize );

        const auto file = throwingOpen( path.c_str(), "rb" );
        const auto nBytesRead = std::fread( fileContents.data(), sizeof( fileContents.front() ), fileSize, file.get() );
        if ( nBytesRead != fileSize ) {
            throw std::runtime_error( "Could not read full file!" );
        }

        std::scoped_lock lock( m_mutex );
        m_fileContents.emplace_back( std::move( fileContents ) );
    }

    [[nodiscard]] size_t
    totalSize() const
    {
        size_t result{ 0 };
        for ( const auto& contents : m_fileContents ) {
            result += contents.size();
        }
        return result;
    }

    [[nodiscard]] size_t
    fileCount() const
    {
        return m_fileContents.size();
    }

private:
    const std::string m_folderToRead;
    std::list<std::vector<char> > m_fileContents;

    std::mutex m_mutex;

    ThreadPool m_threadPool;
};


int
main( int argc, char** argv )
{
    if ( argc < 2 ) {
        std::cerr << "Expected the folder to read to memory as argument!\n";
        return 1;
    }

    FolderReader folderReader( argv[1] );

    const auto t0 = now();
    folderReader.loadAll();
    const auto t1 = now();

    std::cerr << "Loading all files to memory took " << duration( t0, t1 ) << " s "
              << "and resulted in " << folderReader.totalSize() << " B in " << folderReader.fileCount() << " files.\n";

    return 0;
}
