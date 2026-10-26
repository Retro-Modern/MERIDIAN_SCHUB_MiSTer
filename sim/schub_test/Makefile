# Einzeltests fuer SCHUB und die KRAN-Spaltenkopie: make && ./obj_dir/control && ./obj_dir/kran
VFLAGS = --cc --exe --build -j 8 -Wno-fatal -Wno-lint -Wno-style

all: obj_dir/control obj_dir/kran

obj_dir/control: ../../rtl/schub/schub.sv ../../rtl/schub/VexRiscv.v control.cpp
	verilator $(VFLAGS) --top-module schub -o control ../../rtl/schub/schub.sv ../../rtl/schub/VexRiscv.v control.cpp

obj_dir/kran: ../../rtl/kran.sv kran.cpp
	verilator $(VFLAGS) -Mdir obj_kran --top-module kran -o ../obj_dir/kran ../../rtl/kran.sv kran.cpp

clean:
	rm -rf obj_dir obj_kran
