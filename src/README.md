1. Each folder is a self-contained target. It can either be a library or an executable 
or a header only library

*  If MOD_NAME/src/lib.cpp exists, then MOD_NAME will be compiled into a library called libMOD_NAME
*  If MOD_NAME/src/main.cpp exists, then MOD_NAME will be compiled into an executable called MOD_NAME
*  If MOD_NAME/src/include exists and MOD_NAME/src does not exist. Then a header-only will be built

2. Each target also has a /test folder which can be used to have unit tests.

3. Make a copy of the folder and rename it to create a new module

