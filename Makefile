TARGETS := ar7 wp76xx ar86 wp85 localhost

.PHONY: all $(TARGETS)
all: $(TARGETS)

$(TARGETS):
	export TARGET=$@ ; \
		mkapp -v -t $@ \
		-i $(LEGATO_ROOT)/interfaces/airVantage \
		-i $(LEGATO_ROOT)/interfaces/modemServices \
		avcController.adef

clean:
	rm -rf _build_* *.ar7 *.wp7 *.ar86 *.wp85 *.localhost *.update

