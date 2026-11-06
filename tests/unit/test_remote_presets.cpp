/** @file tests/unit/test_remote_presets.cpp */
#include "../tests_common.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <src/remote_presets.h>
#include <string>

namespace {
  namespace fs = std::filesystem;

  struct preset_fixture_t {
    fs::path dir;
    fs::path config;

    preset_fixture_t() {
      const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
      dir = fs::temp_directory_path() / ("sunshine-preset-test-" + std::to_string(suffix));
      fs::create_directories(dir);
      config = dir / "sunshine.conf";
      write("encoder = nvenc\n");
    }

    ~preset_fixture_t() {
      std::error_code ignored;
      fs::remove_all(dir, ignored);
    }

    void write(const std::string &content) {
      std::ofstream out(config, std::ios::binary | std::ios::trunc);
      out << content;
    }

    std::string read() const {
      std::ifstream in(config, std::ios::binary);
      return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
  };
}  // namespace

TEST(RemotePresetsTest, RejectsUnsafeNames) {
  for (const auto *name : {"", "../escape", "a/b", "a\\b", "spaces here", ".hidden", "-flag", "ä"}) {
    EXPECT_FALSE(remote_presets::valid_name(name)) << name;
  }
  EXPECT_TRUE(remote_presets::valid_name("HDR_120fps-AV1"));
  EXPECT_FALSE(remote_presets::valid_name(std::string(49, 'x')));
}

TEST(RemotePresetsTest, SaveListApplyAndRemove) {
  preset_fixture_t files;
  remote_presets::save(files.config, "original", false);
  files.write("encoder = software\n");
  remote_presets::save(files.config, "fallback", false);

  EXPECT_EQ((std::vector<std::string> {"fallback", "original"}), remote_presets::list(files.config));
  EXPECT_THROW(remote_presets::save(files.config, "original", false), std::runtime_error);
  remote_presets::apply(files.config, "original");
  EXPECT_EQ("encoder = nvenc\n", files.read());
  EXPECT_TRUE(fs::exists(files.config.string() + ".before-remote-preset.bak"));

  remote_presets::remove(files.config, "fallback");
  EXPECT_EQ((std::vector<std::string> {"original"}), remote_presets::list(files.config));
}

TEST(RemotePresetsTest, CannotApplyMissingPresetOrTraverseDirectory) {
  preset_fixture_t files;
  EXPECT_THROW(remote_presets::apply(files.config, "missing"), std::runtime_error);
  EXPECT_THROW(remote_presets::apply(files.config, "../other"), std::invalid_argument);
  EXPECT_EQ("encoder = nvenc\n", files.read());
}
