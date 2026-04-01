#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    REQUIRE(in.is_open());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("SamplerEngineLayer header keeps platform-neutral API") {
    const std::filesystem::path headerPath =
        std::filesystem::path(AVANTGARDE_SOURCE_DIR) / "src/app/SamplerEngineLayer.h";
    const std::string src = readFile(headerPath);

    REQUIRE(src.find("platform/") == std::string::npos);
    REQUIRE(src.find("MacAudioHost") == std::string::npos);
    REQUIRE(src.find(".mm") == std::string::npos);
    REQUIRE(src.find("std::shared_ptr<IAudioHost>") != std::string::npos);
}

TEST_CASE("SamplerEngineLayer implementation does not include concrete platform files") {
    const std::filesystem::path cppPath =
        std::filesystem::path(AVANTGARDE_SOURCE_DIR) / "src/app/SamplerEngineLayer.cpp";
    const std::string src = readFile(cppPath);

    REQUIRE(src.find("#include \"platform/") == std::string::npos);
    REQUIRE(src.find("MacAudioHost") == std::string::npos);
    REQUIRE(src.find(".mm") == std::string::npos);
}

