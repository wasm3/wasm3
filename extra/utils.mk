.PHONY: format

all:

format:
	# Convert tabs to spaces
	find ./source -type f -iname '*.[cpp\|c\|h]' -exec bash -c 'expand -t 4 "$$0" | sponge "$$0"' {} \;
	# Remove trailing whitespaces
	find ./source -type f -iname '*.[cpp\|c\|h]' -exec sed -i 's/ *$$//' {} \;
