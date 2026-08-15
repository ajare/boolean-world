#include "Weapon.h"
#include "ModelInstance.h"

namespace applib {

using namespace std;
using namespace wp;

Weapon::Weapon() {
}

BulletWeapon::BulletWeapon()
    : Weapon(), mwBulletMgr(nullptr) {
}

void BulletWeapon::setBulletManager(BulletManager* bulletMgr) {
  mwBulletMgr = bulletMgr;
}

void BulletWeapon::fire(int type, BulletParams const& params, BulletFireParams const& fireParams, Entity* entity) {
  auto const& physicalStats = ModelInstance::entityHandler()->getEntityComponent<PhysicalStats>(*entity);
  float emitAngle = physicalStats.angle + fireParams.angleOffset;

  auto emitPos = physicalStats.position;
  auto emitDir = Vector2::fromAngle(emitAngle, Winding::Clockwise);

  mwBulletMgr->addBullet(type, params, emitPos, emitDir);
}

BeamWeapon::BeamWeapon()
    : Weapon() {
}

void BeamWeapon::setBeamManager(BeamManager* beamMgr) {
  mwBeamMgr = beamMgr;
}

void BeamWeapon::activate(Entity* entity) {
  auto const& beamEmitter = ModelInstance::entityHandler()->getEntityComponent<BeamEmitter>(*entity);

  auto emitPos = beamEmitter.anchorPos;
  auto emitDir = Vector2::fromAngle(beamEmitter.anchorAngle, Winding::Clockwise);
}

void BeamWeapon::deactivate(Entity* entity) {
}

}  // namespace applib