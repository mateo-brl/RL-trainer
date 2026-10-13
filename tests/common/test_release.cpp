#include "tests/TestFramework.h"

#include "core/common/MiniJson.h"

using namespace rlt;

TEST(json_parse_basic) {
    json::Value v;
    CHECK(json::parse(R"({"a": 1.5, "b": [true, false, null], "c": "x\"yé", "d": {"e": "f"}})", v));
    CHECK(v.isObject());
    CHECK_NEAR(v["a"].n, 1.5, 1e-9);
    CHECK(v["b"].isArray() && v["b"].arr.size() == 3 && v["b"].arr[0].b);
    CHECK(v["c"].str() == "x\"y\xC3\xA9");
    CHECK(v["d"]["e"].str() == "f");
    CHECK(v["missing"]["deeper"].str("def") == "def");
    CHECK(!json::parse("{\"a\": }", v));
    CHECK(!json::parse("[1, 2", v));
    CHECK(!json::parse("{} trailing", v));
}

TEST(release_parse_github_latest) {
    const std::string body = R"({
      "url": "https://api.github.com/repos/mateo-brl/RL-trainer/releases/1",
      "html_url": "https://github.com/mateo-brl/RL-trainer/releases/tag/v1.2.0",
      "tag_name": "v1.2.0",
      "name": "RL Trainer 1.2.0",
      "body": "Nouveautés :\n- kickoff",
      "assets": [
        {"name": "RLTrainer-Setup.exe", "browser_download_url": "https://github.com/x/y/releases/download/v1.2.0/RLTrainer-Setup.exe"},
        {"name": "rltrainer.dll", "browser_download_url": "https://github.com/x/y/releases/download/v1.2.0/rltrainer.dll"}
      ]
    })";
    ReleaseInfo r;
    CHECK(parseLatestRelease(body, r));
    CHECK(r.tag == "v1.2.0");
    CHECK(r.version == "1.2.0");
    CHECK(r.dllUrl == "https://github.com/x/y/releases/download/v1.2.0/rltrainer.dll");
    CHECK(r.setupUrl.find("RLTrainer-Setup.exe") != std::string::npos);
    CHECK(r.pageUrl.find("/releases/tag/v1.2.0") != std::string::npos);
    CHECK(!parseLatestRelease(R"({"message": "Not Found"})", r));
    CHECK(!parseLatestRelease("not json", r));
}

TEST(release_compare_versions) {
    CHECK_EQ(compareVersions("1.0.0", "1.0.0"), 0);
    CHECK_EQ(compareVersions("v1.0.10", "1.0.9"), 1);
    CHECK_EQ(compareVersions("1.2", "1.2.0"), 0);
    CHECK_EQ(compareVersions("1.2.0", "1.10.0"), -1);
    CHECK_EQ(compareVersions("2.0.0-beta", "1.9.9"), 1);
    CHECK_EQ(compareVersions("v1.0.37", "v1.0.38"), -1);
}
