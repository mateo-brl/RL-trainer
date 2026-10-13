#include "tests/TestFramework.h"

#include "core/modules/prediction/PredictionModule.h"

using namespace rlt;

TEST(prediction_stub_has_id) {
    PredictionModule m;
    CHECK(std::string(m.id()) == "prediction");
}
