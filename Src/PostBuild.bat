@ECHO OFF

if [%1]==[] goto usage
if [%2]==[] goto usage

Copy "%1" "..\..\LzmaPlugin\%2.dll"
cd ..
cd ..
IF EXIST "UpdateAGKFolder.bat" (
	CALL UpdateAGKFolder.bat
)

@ECHO Copy to each example project.
FOR /D %%G in ("Examples\*") DO (
	Copy "%1" "%%G\Plugins\LzmaPlugin\Windows.dll"
)
goto :eof

:usage
@echo Usage: %~n0 ^<SourceDLLPath^> ^<FinalPluginName^>
exit /b 1