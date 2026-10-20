"""Compatibility entry: now performs actual two-board EIDE compile and link."""
import sys
sys.dont_write_bytecode=True
from check_eide_link import main
if __name__=='__main__':raise SystemExit(main())
