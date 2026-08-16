#include "ProtoEntityDefinitionFactory.h"
#include "GameException.h"

using namespace std;

ProtoEntityDefinitionFactory::ProtoEntityDefinitionFactory()
    : applib::ProtoEntityDefaultDefinitionFactory() {
}

vector<string> ProtoEntityDefinitionFactory::getExtraPropertyNames() const {
  return {"Stats"};
}

uint32_t ProtoEntityDefinitionFactory::getAnimationIdFromName(string const& actor, string const& anim) {
  VAR_UNUSED(actor);
  VAR_UNUSED(anim);

  return 0;
}