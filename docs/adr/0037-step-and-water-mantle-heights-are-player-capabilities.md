# Step and water-mantle heights are player capabilities

**Status:** Accepted

Ordinary floor-step traversal is limited by the player's fixed step height, not by a World-authored threshold. The serialized World threshold introduced by ADR-0006 is removed: allowing a World to set it to infinity can make arbitrarily deep floor discontinuities horizontally traversable, while differing player mobility is not part of the World domain. Mantling out of water has its own fixed player capability, measured as the absolute elevation difference between the player's eye and the adjacent floor. This supersedes ADR-0006.
