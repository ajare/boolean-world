#pragma once

#include <string>

#include "Platform.h"
#include "Entity.h"
#include "BulletManager.h"
#include "BeamManager.h"

namespace applib {

class Weapon {
public:
  Weapon();

  virtual ~Weapon() = default;
};

class BulletWeapon : public Weapon {
  BulletManager* mwBulletMgr;

public:
  BulletWeapon();

  void setBulletManager(BulletManager* bulletMgr);

  void fire(int type, BulletParams const& params, BulletFireParams const& fireParams, Entity* entity);
};

class BeamWeapon : public Weapon {
  BeamManager* mwBeamMgr;

public:
  BeamWeapon();

  void setBeamManager(BeamManager* beamMgr);

  void activate(Entity* entity);

  void deactivate(Entity* entity);
};

}  // namespace applib