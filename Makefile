.PHONY: compiler test book check clean deploy uninstall

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

# Installs compiler/przp + compiler/std to $PREFIX (default /usr/local) —
# see deploy.sh. A bare `cp przp /usr/local/bin/` is not enough on its
# own: przp needs std/ deployed alongside it too.
deploy:
	./deploy.sh

uninstall:
	./deploy.sh --uninstall
