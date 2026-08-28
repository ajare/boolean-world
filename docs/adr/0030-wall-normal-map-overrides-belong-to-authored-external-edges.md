# Wall normal-map overrides belong to authored External edges

**Status:** Accepted

A wall normal-map override belongs to one authored External edge of a MeshPrimitive, not to its Sub-material or to a generated ArrangementWall. Sub-material ownership cannot permit two walls using the same Sub-material to choose different maps, while an ArrangementWall is derived by the boolean fold and has no stable authored identity; the External edge is the persistent surface boundary already used for wall-specific overrides. The override is tri-state (Unset, Disabled, or Image), resolves by boolean-fold precedence when edges coincide, and applies to the uncut wall surface only—not to new facets exposed by Chips.
