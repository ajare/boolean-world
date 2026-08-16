# Remove the dormant combat framework

**Status:** Accepted

Willpower.Firepower and AppLib's generic bullets, beams, and weapons formed a second collision and rendering framework, but BooleanWorld never supplied its mesh collision manager or used its combat APIs. The application still constructed empty managers and carried a mandatory `GameResource` through Launcher and state transitions solely to support this dormant framework.

## Decision

Delete Willpower.Firepower and the complete AppLib combat surface rather than preserve unused compatibility abstractions. This includes projectile and beam types, managers and renderers, mesh-collision lifecycle code, combat resource definitions, and the `GameResource` launch and transition contract. Future combat behavior must be designed from concrete game requirements and integrated with the active world systems instead of reviving the generic framework.

The affected launcher and prototype-property schemas reject unknown fields. Retired `GameResource` and `BeamEmitter` values therefore fail like misspelled fields, while a retired `Game` resource fails through the ordinary unsupported resource-type path. Historical documents may continue to describe the removed framework.

## Consequences

- BooleanWorld no longer constructs empty projectile or beam managers on every play session.
- AppLib no longer promises generic combat, projectile rendering, or mesh collision.
- Willpower.Viz retains only renderers used independently of Firepower.
- Launcher configuration no longer requires a resource with no runtime purpose.
