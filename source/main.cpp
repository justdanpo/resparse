#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <deque>
#include <iostream>

#include <wil/resource.h>
#include <argy.hpp>

struct range
{
    LONGLONG start;
    LONGLONG end;
};

class resparser
{
public:
    resparser(const char *const fileName, bool verbose = false)
        : verbose(verbose),
          fileHandle(wil::unique_hfile(CreateFileA(
              fileName,
              (GENERIC_READ | GENERIC_WRITE), 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)))
    {
        if (!fileHandle)
        {
            throw std::exception("Cannot open file");
        }

        LARGE_INTEGER fsData;
        if (!GetFileSizeEx(fileHandle.get(), &fsData))
        {
            throw std::exception("Cannot get file size");
        }
        else
        {
            fileSize = fsData.QuadPart;
        }
    }

    void resparse(DWORD blockSize)
    {
        this->blockSize = blockSize;

        if (!io_set_sparse_flag())
        {
            throw std::exception("Cannot set sparse");
        }

        process_file(get_allocated_ranges());
    }

private:
    DWORD blockSize;
    bool verbose;
    wil::unique_hfile fileHandle;
    LONGLONG fileSize;
    LONGLONG deallocated_bytes;
    std::deque<FILE_ZERO_DATA_INFORMATION> deallocateQueue;

    bool io_set_sparse_flag()
    {
        if (verbose)
        {
            std::cout << "Set sparse mode" << std::endl;
        }

        DWORD bytesReturned;
        return DeviceIoControl(
            fileHandle.get(),
            FSCTL_SET_SPARSE,
            NULL, 0,
            NULL, 0,
            &bytesReturned,
            NULL);
    }

    void io_deallocate_range(FILE_ZERO_DATA_INFORMATION const &range)
    {
        if (verbose)
        {
            std::cout << "Deallocating bytes [" << range.FileOffset.QuadPart << "..." << range.BeyondFinalZero.QuadPart << ") ...";
        }

        DWORD dwOut;
        if (!DeviceIoControl(
                fileHandle.get(),
                FSCTL_SET_ZERO_DATA,
                (LPVOID)&range, sizeof(range),
                NULL, 0,
                &dwOut,
                NULL))
        {
            throw std::exception("Cannot set zero data");
        }

        if (verbose)
        {
            std::cout << "Done" << std::endl;
        }

        deallocated_bytes += range.BeyondFinalZero.QuadPart - range.FileOffset.QuadPart;
    }

    std::deque<range> get_allocated_ranges()
    {
        LONGLONG totalAllocated = 0;
        std::deque<range> result;

        bool done = false;
        for (LONGLONG pos = 0; !done && pos < fileSize;)
        {
            FILE_ALLOCATED_RANGE_BUFFER queryRange;
            queryRange.FileOffset.QuadPart = pos;
            queryRange.Length.QuadPart = fileSize - pos;

            FILE_ALLOCATED_RANGE_BUFFER buff[64];
            DWORD bytesReturned = 0;

            done = DeviceIoControl(
                fileHandle.get(),
                FSCTL_QUERY_ALLOCATED_RANGES,
                &queryRange, sizeof(queryRange),
                buff, sizeof(buff),
                &bytesReturned,
                NULL);
            if (!done && ERROR_MORE_DATA != GetLastError())
            {
                throw std::exception("Cannot get allocated ranges");
            }

            std::size_t count = bytesReturned / sizeof(buff[0]);

            for (std::size_t itemIndex = 0; itemIndex < count; itemIndex++)
            {
                LONGLONG start = buff[itemIndex].FileOffset.QuadPart;
                LONGLONG len = buff[itemIndex].Length.QuadPart;
                LONGLONG end = start + len;

                if (verbose)
                {
                    std::cout << "Found allocated range [" << start << "..." << end << ")" << std::endl;
                }

                result.push_back({start, end});
                totalAllocated += len;

                pos = end;
            }
        }

        if (verbose)
        {
            std::cout << "File size: " << fileSize << std::endl;
            std::cout << "Allocated: " << totalAllocated << std::endl;
        }

        return result;
    }

    void deallocate_remained()
    {
        for (auto const &range : deallocateQueue)
        {
            io_deallocate_range(range);
        }

        deallocateQueue.clear();
    }

    void deallocate_next(FILE_ZERO_DATA_INFORMATION const &range)
    {
        if (!deallocateQueue.empty())
        {
            if (deallocateQueue.back().BeyondFinalZero.QuadPart == range.FileOffset.QuadPart)
            {
                deallocateQueue.back().BeyondFinalZero.QuadPart = range.BeyondFinalZero.QuadPart;
                return;
            }

            io_deallocate_range(deallocateQueue.front());
            deallocateQueue.pop_front();
        }

        deallocateQueue.push_back(range);
    }

    LONGLONG get_file_deallocated_bytes()
    {
        FILE_STANDARD_INFO fileInfo;
        if (!GetFileInformationByHandleEx(fileHandle.get(), FileStandardInfo, &fileInfo, sizeof(fileInfo)))
        {
            throw std::exception("Cannot get file info");
        }

        return fileInfo.EndOfFile.QuadPart - fileInfo.AllocationSize.QuadPart;
    }

    void process_file(std::deque<range> allocatedRanges)
    {
        deallocated_bytes = 0;

        std::vector<char> buff(blockSize);
        for (LONGLONG pos = 0; pos < fileSize;)
        {
            if (!allocatedRanges.empty() && pos >= allocatedRanges.front().end)
            {
                // out of current range, go to the next one
                allocatedRanges.pop_front();
                if (allocatedRanges.empty())
                {
                    break;
                }
                else
                {
                    pos = allocatedRanges.front().start;
                    continue;
                }
            }

            LARGE_INTEGER posToSet;
            posToSet.QuadPart = pos;
            if (!SetFilePointerEx(fileHandle.get(), posToSet, NULL, FILE_BEGIN))
            {
                throw std::exception("Cannot set file pointer");
            }

            LONGLONG requiredSize = std::min(static_cast<LONGLONG>(blockSize), allocatedRanges.front().end - pos);
            DWORD bytesRead;
            if (!ReadFile(fileHandle.get(), buff.data(), static_cast<DWORD>(requiredSize), &bytesRead, NULL) || bytesRead == 0)
            {
                throw std::exception("Cannot read file part");
            }

            bool isFilledByZeroes = std::all_of(buff.begin(), buff.begin() + bytesRead, [](char i)
                                                { return i == 0; });

            if (isFilledByZeroes)
            {
                FILE_ZERO_DATA_INFORMATION range;
                range.FileOffset.QuadPart = pos;
                range.BeyondFinalZero.QuadPart = pos + bytesRead;

                deallocate_next(range);
            }

            pos += bytesRead;
        }

        deallocate_remained();

        std::cout
            << "Done! " << deallocated_bytes << " bytes zeroed. Total deallocated bytes reported by OS: "
            << get_file_deallocated_bytes() << std::endl;
    }
};

int main(int argc, char *argv[])
{
    Argy::CliParser cli(argc, argv);

    try
    {
        cli.setHelpHeader("resparse v1.0");

        cli.addString("input", "Input file path");
        cli.addInt({"-b", "--blockSize"}, "Block size", 65536).isInRange(512, 1024 * 1024 * 1024);
        cli.addBool({"-v", "--verbose"}, "Enable verbose output");

        auto args = cli.parse();

        resparser resparser_obj(args.getString("input").c_str(), args.getBool("verbose"));
        resparser_obj.resparse(args.getInt("blockSize"));
        return 0;
    }
    catch (const Argy::Exception &ex)
    {
        std::cerr << "Error: " << ex.what() << "\n";
        cli.printHelp(argv[0]);
        return 1;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    catch (...)
    {
        std::cerr << "Unknown error " << "\n";
        return 1;
    }

    return 0;
}
