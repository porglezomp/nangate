all: test

test: example/example.sv example/example.ys nangate.so
	yosys example/example.ys

%_pm.h: %.pmg
	pmgen $< $@

nangate.so: nangate.cc erase_b2f_pm.h dff_nan_pm.h share_nan_pm.h
	yosys-config --build $@ $<

clean:
	rm -rf -- *.d *.so *.so.dSYM *_pm.h
