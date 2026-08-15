# Interpolators
Interpolators are functions which takes a time value and produce a derived value.  The time value is a continuous value from
zero up to a defined maximum, and the return value is of any time - although at the moment, only floating point values are
implemented - and can be within an arbitrary user-defined range.

An interpolator is defined by several keyframes, and there must always be keyframes at zero and one.  Each segment between
the frames is then given an "easing" function - by default, linear - which interpolates the value between the keyframes.
## Easing functions
There are a large number of these, and the best way to learn them is to look at them in the interpolator editor UI widget.
## Scale
By default, both the time (x-axis) and value (y-axis) of an interpolator are clamped to [0, 1].  These can both by modified,
however the minimum time cannot be less than zero, and must of course be less than the maximum time.
## Discontinuities
It is possible to have keyframes of zero length.  This will give the effect of a value jumping from one value to another,
rather than smoothly blending.