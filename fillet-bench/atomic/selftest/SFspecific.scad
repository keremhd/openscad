// atomic-case:      SFspecific
// atomic-title:     selftest fixture: the most specific selector must win
// atomic-family:    selftest
// atomic-class:     saddle
// atomic-select:    both
// atomic-camera:    0,0,0 | 1,1,1 | 10
// atomic-expect:    fn=*      op=*        tube=1 *=0
// atomic-expect:    fn=*      op=fillet   cap=1 *=0
// atomic-expect:    fn=stock  op=*        coons=1 *=0
// atomic-expect:    fn=stock  op=fillet   saddle=2 *=0
// mesh.py-comp:     1
