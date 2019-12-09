#
# Utility to disassemble a specific function.
# Usage:
# ./disasm-func.sh ../build/wasm3 i32_Divide_sr
#

FILE="$1"
FUNC="$2"

# get starting address and size of function fact from nm
ADDR=$(nm --print-size --radix=d $FILE | grep $FUNC | cut -d ' ' -f 1,2)
# strip leading 0's to avoid being interpreted by objdump as octal addresses
STARTADDR=$(echo $ADDR | cut -d ' ' -f 1 | sed 's/^0*\(.\)/\1/')
SIZE=$(echo $ADDR | cut -d ' ' -f 2 | sed 's/^0*//')
STOPADDR=$(( $STARTADDR + $SIZE ))

objdump --disassemble $FILE --start-address=$STARTADDR --stop-address=$STOPADDR -M suffix
