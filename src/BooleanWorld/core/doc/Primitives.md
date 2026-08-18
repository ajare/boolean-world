# Primitives
Primitives are the building blocks of the world.  They are programmatic objects, and as such can have the parameters that
define them dynamically altered.

Primitives have the usual scale/rotate/translate transform applied to them on top of their basic vertices.  Further, they
can be rotated around and additional point ("orbit").
## Operation
### Union
The output is the combined outline of input polygons, regardless of how they intersect.
### Difference
This is the only non-commutative operation, and for operation A diff B, B is "subtracted" or cut from A, if they intersect.
### Intersection
The output is the areas where the input polygons overlap.
### XOR
The output is the areas where exactly one polygon exists.  In other words this is the same as doing both a union and an intersection,
and doing a difference of those two results.
## Fill rule
Complex polygons are defined by one or more closed contours that set both outer and inner polygon boundaries. Only portions of these
contours may set polygon boundaries, so crossing a contour may or may not mean entering or exiting a filled polygon region. For this
reason complex polygons require filling rules that define which polygon sub-regions are considered inside a given polygon.
### NonZero
Only non-zero sub-regions are filled.
### Even-Odd
Only odd numbered sub-regions are filled.
## Priority
The order of operations on primitivese makes a difference to the final result.  The priority is a value in [0, 256] where lower values
get their operations done first.  The very first polygon's operation is ignored, and the first operation is performing the second
polygon's operation on the first, then the third polygon's operation on the result of that, and so on.
## Basic types
### RegularPolygon
This primitive has between 3 and 1024 sides (inclusive), with the first vertex pointing up, so a 4-sided primitive is diamond-
shaped rather than square shaped.
### Circle
This is a standard circle, with variable resolution.
#### Resolution
A finite value in [3/64, 1] which acts as a multiplier for the circle's base resolution of 64 vertices.
### CircleSegment
A circle but only with a given arc length (centred at angle 0), with variable resolution.
#### Resolution
A finite value in [3/64, 1] which acts as a multiplier for the segment's base resolution of 64 arc-boundary vertices.
#### ArcLength
A finite value in [0.01, 360] giving the number of degrees the segment spans, centred on 0.
### RectanglePolygon
#### X/Y ratio
A finite width-to-height ratio in [1, 10].
### SuperformulaPolygon
An implementation of the [Superformula](https://en.wikipedia.org/wiki/Superformula). This takes 6 parameters and produces different shapes.
#### Resolution
A finite positive value controlling contour sampling density, bounded at approximately 2.55 so the contour cannot exceed 1024 vertices.
#### Values
Exactly 6 finite float parameters (`a`, `b`, `m`, `n1`, `n2`, and `n3`) define the superformula. The denominators `a` and `b` and the exponents `n1`, `n2`, and `n3` must be positive.
### Torus
A torus/doughnut shape, with variable resolution and thickness.
#### Resolution
A finite value in [3/64, 1] which acts as a multiplier for the torus's base resolution of 64 vertices per contour.
#### Thickness
A finite radial width in [0.01, 0.99], relative to the torus radius.
### TorusSegment
A torus/doughnut shape but only with a given arc length (centred at angle 0), with variable resolution and thickness.
#### Resolution
A finite value in [3/64, 1] which acts as a multiplier for the segment's base resolution of 64 arc-boundary vertices.
#### Thickness
A finite radial width in [0.01, 0.99], relative to the torus radius.
#### ArcLength
A finite value in [0.01, 360] giving the number of degrees the segment spans, centred on 0.
