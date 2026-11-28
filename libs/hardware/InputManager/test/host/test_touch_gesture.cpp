#include "../../include/TouchGesture.h"
#include <cassert>
#include <initializer_list>
using G=freeink::TouchGesture;
int main() {
  G g;
  for(int sign:{-1,1}) {
    g.begin(100,100,0);assert(g.finish(100+sign*48,100,1200)==G::Result::Swipe);
    g.begin(100,100,0);assert(g.finish(100,100+sign*48,1201)==G::Result::None);
    g.begin(100,100,0);assert(g.finish(100,100+sign*47,200)==G::Result::Tap);
  }
  g.begin(100,100,0);g.sample(110,110,500);assert(g.finish(110,20,700)==G::Result::None);
  g.begin(100,100,0);g.sample(100,30,100);assert(g.finish(101,100,200)==G::Result::None);
  g.begin(100,100,0);g.sample(100,65,400);assert(g.finish(100,30,1100)==G::Result::Swipe);
  g.begin(100,100,0);assert(g.finish(103,105,50)==G::Result::Tap);
  g.begin(100,100,0);assert(g.finish(100,100,600)==G::Result::None);
}
