# Copy to llvm_prov_build to be packaged in artifact.
configure_file(llvm-prov-make ${CMAKE_CURRENT_BINARY_DIR} COPYONLY)
configure_file(xtools.sh ${CMAKE_CURRENT_BINARY_DIR} COPYONLY)
