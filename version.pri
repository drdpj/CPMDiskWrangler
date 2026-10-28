# Shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler. GPL v3 or later.
#
# The program's version, from git when qmake runs: "git describe --tags --always --dirty" gives the tag
# (v0.9.0-beta.1), or the tag plus commits since and the commit (v0.9.0-beta.1-3-gabc1234), with "-dirty" when
# there are uncommitted changes. Without git or its history (a source zip) it is "unknown", unless given:
#     qmake APP_VERSION=v1.0.0 ...
# It is compiled in as APP_VERSION (shown in Help > About). The x.y.z at its start, when there is one, also
# becomes qmake's VERSION: the version in Explorer's file properties and in the macOS app's Info.plist.
# qmake reads it when it runs, so re-run qmake to pick up a new commit or tag.

isEmpty(APP_VERSION) {
    win32: NULLDEV = nul
    else:  NULLDEV = /dev/null
    APP_VERSION = $$system(git -C $$shell_quote($$PWD) describe --tags --always --dirty 2> $$NULLDEV)
}
isEmpty(APP_VERSION): APP_VERSION = unknown
DEFINES += APP_VERSION=\\\"$$APP_VERSION\\\"

APP_VERSION_NUMBER = $$replace(APP_VERSION, "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+).*$", "\\1.\\2.\\3")
contains(APP_VERSION_NUMBER, "^[0-9]+\\.[0-9]+\\.[0-9]+$"): VERSION = $$APP_VERSION_NUMBER
message("$$TARGET version: $$APP_VERSION")
