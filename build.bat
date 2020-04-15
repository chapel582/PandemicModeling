@echo off

call .\setup.bat

set BuildFolder=.\build
set CommonCompilerFlags=-MTd -nologo -GR- -EHa- -Oi -Od -W4 -FC -Z7
set CommonLinkerFlags=-opt:ref

IF NOT EXIST %BuildFolder% mkdir %BuildFolder%
pushd %BuildFolder%
del *.pdb > nul 2> nul
cl %CommonCompilerFlags% ..\pandemic_model.cpp -Fpandemic_model.map /link %CommonLinkerFlags%
popd