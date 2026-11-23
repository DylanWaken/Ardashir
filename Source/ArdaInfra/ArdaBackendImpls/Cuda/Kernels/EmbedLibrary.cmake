# Embed the provider's small stock transfer library for legacy package commands.
file(READ "${INPUT}" Bytes HEX)
string(REGEX REPLACE "(..)" "0x\\1,\n" Initializer "${Bytes}")
file(WRITE "${OUTPUT}" "${Initializer}")
