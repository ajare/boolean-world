#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include <core/Serializable.h>

namespace {

class DestructorProbe final : public bw::core::Serializable {
public:
  ~DestructorProbe() override {
    destroyed = true;
  }

  static inline bool destroyed = false;

private:
  bool childrenModified() const override {
    return false;
  }

  void serializeImpl(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) const override {
  }

  bool deserializeImpl(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) override {
    return false;
  }
};

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void deletingThroughSerializableDestroysDerivedObject() {
  static_assert(std::has_virtual_destructor_v<bw::core::Serializable>);

  DestructorProbe::destroyed = false;
  bw::core::Serializable* serializable = new DestructorProbe;
  delete serializable;

  require(DestructorProbe::destroyed,
          "deleting through Serializable did not destroy the derived object");
}

}  // namespace

int main() {
  try {
    deletingThroughSerializableDestroysDerivedObject();
    std::cout << "Serializable safely destroys derived objects\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
