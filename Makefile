.PHONY: compiler test book check clean

compiler:
	$(MAKE) -C compiler

test:
	bash tests/run.sh

book:
	mdbook build book

check: compiler test book

clean:
	$(MAKE) -C compiler clean
	rm -rf tests/tmp
