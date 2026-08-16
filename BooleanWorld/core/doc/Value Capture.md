# ValueCapture
This is an enum which defines how input values are clamped.  At a basic level, values can either be *Sticky*, *Delta* or *Latched*.
## Sticky
This means that input maps directly to output.
## Delta up/down
If delta up or down, then the output only increases by the delta between the input and the previous input, if the delta sign matches
the up/down type.
## Latched up/down
If latched up or down, then the output value only changes if the input is greater (or less) than the previous input.