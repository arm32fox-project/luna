# This script is invoked by Jenkins to execute
# incremental builds and upload the produced *.dmg
# files to the distribution mirror.

# Since it is intended to be checked in and public, please
# do not commit sensitive information e.g. server or port here.

export PATH=$PATH:/usr/local/bin
export MOZCONFIG=$(pwd)/build/macosx/universal/mozconfig.$FLAVOR

rm -rf obj-*darwin*/dist

./mach build
./mach package

pushd obj-*darwin*/dist
scp -P ${PORT} palemoon-*.dmg ${DIST}/$(ls palemoon-*.dmg | sed "s/\.en-US/-${BUILD_NUMBER}-${FLAVOR}.en-US/")
popd
