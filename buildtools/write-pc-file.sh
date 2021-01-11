#!/bin/sh

# Write pkg-config (.pc) file

if [ ! "$VERSION" ]; then
    echo "All of VERSION CXXLINKOPTS CXXOPTS HEADERDIR HEADERDIR_ISSYS must be set." 
    exit 1
fi

HEADERDIR_INC=""
if [ "$HEADERDIR_ISSYS" != TRUE ]; then
    HEADERDIR_INC="-I${HEADERDIR}"
fi

echo "Writing dasynq.pc file."
rm -f dasynq.pc

echo "# Dasynq - event loop library" >> dasynq.pc
echo "Name: Dasynq" >> dasynq.pc
echo "Description: Thread-safe cross-platform event loop library in C++" >> dasynq.pc
echo "Version: ${VERSION}" >> dasynq.pc
echo "URL: https://github.com/davmac314/dasynq" >> dasynq.pc
echo "Libs: ${CXXLINKOPTS}" >> dasynq.pc
echo "Cflags: ${CXXOPTS} ${HEADERDIR_INC}" >> dasynq.pc
