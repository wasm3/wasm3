.PHONY: format

all:

format:
	# Convert tabs to spaces
	find ./source -type f -iname '*.[cpp\|c\|h]' -exec bash -c 'expand -t 4 "$$0" | sponge "$$0"' {} \;
	# Remove trailing whitespaces
	find ./source -type f -iname '*.[cpp\|c\|h]' -exec sed -i 's/ *$$//' {} \;

preprocess: preprocess_restore
	cp ./source/m3_exec.h    ./source/m3_exec.h.bak
	cp ./source/m3_compile.c ./source/m3_compile.c.bak
	awk 'BEGIN {RS=""}{gsub(/\\\n/,"\\\n__NL__ ")}1' ./source/m3_exec.h | sponge ./source/m3_exec.h
	cpp -P ./source/m3_compile.c | sponge ./source/m3_compile.c
	awk '{gsub(/__NL__/,"\n")}1' ./source/m3_compile.c | sponge ./source/m3_compile.c
	mv ./source/m3_exec.h.bak ./source/m3_exec.h

preprocess_restore:
	-mv source/m3_compile.c.bak source/m3_compile.c
	touch source/m3_compile.c


