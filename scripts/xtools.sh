check_tool()
{
	prefix=$1
	varname=$2
	tool=$3

	if ! [ -e "${prefix}/bin/${tool}" ]
	then
		echo "bin/${tool} does not exist in prefix ${prefix}"
		exit 1
	fi

	if ! [ -x "${prefix}/bin/${tool}" ]
	then
		echo "${prefix}/bin/${tool} is not executable"
		exit 1
	fi

	export X${varname}=${prefix}/bin/${tool}
}

check_llvm_prefix()
{
	if [ "${LLVM_PREFIX}" = "" ]
	then
		llvm_link=`which llvm-link`
		if [ -e "${llvm_link}" ]
		then
			LLVM_PREFIX=`dirname ${llvm_link} | xargs dirname`
		else
			echo "LLVM_PREFIX not set and no llvm-link in PATH"
			exit 1
		fi
	fi
}

find_llvm_prov_libraries()
{
	#
	# Find the LLVMProv library:
	#
	#  - as explicitly specified with LLVM_PROV_LIB
	#  - within LLVM_PREFIX
	#

	loomlib=LLVMLoom.so

	if [ "${LOOM_LIB}" = "" ]
	then
		if [ -e "${LOOM_PREFIX}/lib/${loomlib}" ]
		then
			LOOM_LIB="${LOOM_PREFIX}/lib/${loomlib}"
		elif [ -e "${LLVM_PREFIX}/lib/${loomlib}" ]
		then
			LOOM_LIB="${LLVM_PREFIX}/lib/${loomlib}"
		else
			echo "LOOM_LIB not specified, no ${loomlib} in LOOM_PREFIX or LLVM_PREFIX"
			exit 1
		fi
	fi

	libname=LLVMProv.so

	if [ "${LLVM_PROV_LIB}" = "" ]
	then
		if [ -e "${LLVM_PROV_PREFIX}/lib/${libname}" ]
		then
			LLVM_PROV_LIB="${LLVM_PROV_PREFIX}/lib/${libname}"
		elif [ -e "${LLVM_PREFIX}/lib/${libname}" ]
		then
			LLVM_PROV_LIB="${LLVM_PREFIX}/lib/${libname}"
		else
			echo "LLVM_PROV_LIB not specified, no ${libname} in LLVM_PROV_PREFIX or LLVM_PREFIX"
			exit 1
		fi
	fi
}
