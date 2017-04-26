

extern "C" {
__attribute__ ((visibility ("default"))) IProc *initProc() {
		return new Proc;
	}
}
