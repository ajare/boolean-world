#pragma once

#include <applib/ProtoEntityDefaultDefinitionFactory.h>

#include "Platform.h"

class ProtoEntityDefinitionFactory : public applib::ProtoEntityDefaultDefinitionFactory {
protected:
  std::vector<std::string> getExtraPropertyNames() const override;
  uint32_t getAnimationIdFromName(std::string const& actor, std::string const& anim) override;

public:
  ProtoEntityDefinitionFactory();
};
