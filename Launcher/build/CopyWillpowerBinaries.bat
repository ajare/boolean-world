SET VSVER=%1
SET PLATFORM=%2
SET CONFIGURATION=%3
SET BASEDIR=%~dp0
SET PROJECTDIR=%BASEDIR%\%VSVER%\
SET SRCDIR=%VSVER%\bin\%PLATFORM%\%CONFIGURATION%
SET TDPDIR=bin\%VSVER%\%PLATFORM%\%CONFIGURATION%
SET OUTDIR=%PROJECTDIR%bin\%PLATFORM%\%CONFIGURATION%

echo Copying binaries to %OUTDIR%

copy /Y "%PROJECTDIR%..\support\%COMPUTERNAME%\%VSVER%\%PLATFORM%\%CONFIGURATION%\*.*" "%OUTDIR%"

copy /Y "%PROJECTDIR%..\..\..\vendor\bin\%VSVER%\%PLATFORM%\%CONFIGURATION%\*.*" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\Willpower\willpower.common\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\Willpower\willpower.application\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\Willpower\willpower.collide\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\Willpower\willpower.editor\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\Willpower\willpower.firepower\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\Willpower\willpower.geometry\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\Willpower\willpower.serialization\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\Willpower\willpower.viz\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\Willpower\willpower.wayfinder\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\AppLib\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\ext\MassivePolyPusher\mpp\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\ext\MassivePolyPusher\mpp-mesh\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\ext\MassivePolyPusher\mpp-program\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\ext\MassivePolyPusher\mpp-helper\build\%SRCDIR%\*.dll" "%OUTDIR%"
copy /Y "%PROJECTDIR%..\..\..\ext\Utils\build\%SRCDIR%\*.dll" "%OUTDIR%"
