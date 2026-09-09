#include "TaskPackTestPack.h"
#include "atlas/Extension/TaskPack.h"

#include <iostream>
#include <string_view>

/** @brief Test-only process for creating a native fixture and comparing inspection with JSON Schema. */
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            return 2;
        const std::filesystem::path directory{ argv[2] };
        if (std::string_view{ argv[1] } == "fixture")
        {
            Atlas::Testing::TaskPackTestPack pack;
            std::filesystem::copy(pack.directory, directory, std::filesystem::copy_options::recursive);
        }
        else if (std::string_view{ argv[1] } != "inspect")
            return 2;
        Atlas::TaskPackRegistry registry;
        std::cout << registry.inspectDirectory(directory).digest << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
