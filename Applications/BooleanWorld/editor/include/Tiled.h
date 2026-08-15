#pragma once

#include <string>
#include <memory>

#include <core/World.h>


void openTiledPrefabFile(std::string const& filepath, std::shared_ptr<bw::core::World> world);
