#include <iostream>
#include <memory>
#include <stdexcept>

#include <core/Serializable.h>

namespace {

class DeserializationProbe final : public bw::core::Serializable {
public:
  explicit DeserializationProbe(bool result)
      : mResult(result) {
  }

private:
  bool mResult;

  bool childrenModified() const override {
    return false;
  }

  void serializeImpl(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) const override {
  }

  bool deserializeImpl(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) override {
    return mResult;
  }
};

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void successfulDeserializationLeavesObjectUnmodified() {
  DeserializationProbe probe(true);
  bw::core::SerializationWorkData workData;

  require(probe.isModified(), "new Serializable was unexpectedly unmodified");
  require(probe.deserialize(nullptr, workData),
          "successful deserializeImpl result was not returned");
  require(!probe.isModified(),
          "freshly deserialized Serializable was marked modified");
}

void failedDeserializationReturnsFailureIndependentlyOfModifiedState() {
  DeserializationProbe probe(false);
  bw::core::SerializationWorkData workData;

  require(!probe.deserialize(nullptr, workData),
          "failed deserializeImpl result was not returned");
  require(!probe.isModified(),
          "deserialization result leaked into the modified state");
}

}  // namespace

int main() {
  try {
    successfulDeserializationLeavesObjectUnmodified();
    failedDeserializationReturnsFailureIndependentlyOfModifiedState();
    std::cout << "Serializable deserialization modification coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
