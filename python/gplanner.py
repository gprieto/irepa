from pinocchio.utils import *
from numpy.linalg import inv,norm
import math
import time
import random
import matplotlib.pylab as plt

from prm import *
from oprm import OptimalPRM
from grid_policy import GridPolicy,onehot
from cursor_tricks import FromCursor

from specpath import acadoBinDir,acadoTxtPath
from bicopter import Bicopter
from acado_connect import AcadoConnect

dataRootPath = 'data/planner/bicopter'
     
# --- PENDULUM DOUBLE ------------------------------------------------------------
# --- PENDULUM DOUBLE ------------------------------------------------------------
# --- PENDULUM DOUBLE ------------------------------------------------------------

class GraphBicopter(Graph):
     '''Specialization of Graph to have the display dedicated to double pendulum.'''
     def __init__(self): Graph.__init__(self)

     def plotState(self,q,v=None,color='r',marker='o',lseg=.1):
          q = q.flat
          plt.plot([q[0]+np.cos(q[2])*lseg,q[0]-np.cos(q[2])*lseg],
                   [q[1]+np.sin(q[2])*lseg,q[1]-np.sin(q[2])*lseg],
                   linestyle='-',marker=marker,linewidth=1,color=color)

     def plotNode(self,idx,plotstr='k',**kwargs):
          x = self.x[idx]
          if x is None: return
          self.plotState(x,color=plotstr)
          plt.plot(x[0,0],x[1,0],plotstr,**kwargs)

     def plotEdge(self,idx1,idx2,plotstr='k',withTruePath = False,**kwargs):
          path = self.states[idx1,idx2]
          if withTruePath: 
               plt.plot(path[:,0],path[:,1],plotstr,**kwargs)
          else: 
               plt.plot(path[[0,-1],0],path[[0,-1],1],plotstr,**kwargs)

def config(acado,label,env=None):
     acado.options['thetamax']    = np.pi/2
     acado.options['printlevel']    = 1
     if 'shift' in acado.options: del acado.options['shift']
     if env is not None:
          acado.options['umax']     = "%.2f %.2f" % tuple([x for x in env.umax])
          
     if label == "connect":
          if 'icontrol' in acado.options: del acado.options['icontrol']
          acado.debug(False)
          acado.iter                = 20
          acado.options['steps']    = 20
          acado.setTimeInterval(5.)

     elif label == "traj":
          if 'horizon' in acado.options: del acado.options['horizon']
          if 'Tmax'    in acado.options: del acado.options['Tmax']
          acado.debug(False)
          acado.iter                = 80
          acado.options['steps']    = 30
          acado.options['icontrol'] = acadoTxtPath+'guess.clt'
          
     elif label == "policy":
          if 'horizon' in acado.options: del acado.options['horizon']
          if 'Tmax'    in acado.options: del acado.options['Tmax']
          acado.debug(False)
          acado.iter                = 80
          acado.options['steps']    = 20
          acado.options['icontrol'] = acadoTxtPath+'guess.clt'

     elif label == "refine":
          if 'horizon' in acado.options: del acado.options['horizon']
          if 'Tmax'    in acado.options: del acado.options['Tmax']
          acado.debug(False)
          acado.iter                = 80
          acado.options['steps']    = 20
          acado.options['icontrol'] = acadoTxtPath+'guess.clt'

class ConnectAcado(ConnectAbstract):
     def __init__(self,acado):
          self.acado = acado

     def __call__(self,x1,x2,verbose=False):
          try: return self.acado.run(x1,x2)
          except: return False

     def states(self):
          return self.acado.states()
     def controls(self): 
          return self.acado.controls()
     def cost(self):
          return self.acado.opttime()
     def times(self):
          return self.acado.times()
     def time(self):
          return self.acado.opttime()

class BicopterStateDiff:
     def __call__(self,x1,x2):
          return x2-x1

def checkPRM(graph,verbose=False):
     errors = 0
     for (i0,i1),X in graph.states.items():
          if not np.allclose(X[0 ,:],graph.x[i0].flat):
               if verbose: print 'Error at from %3d-%3d \t(%.2f)'\
                  %(i0,i1,norm(X[0 ,:]-graph.x[i0].flat))
               errors +=1
          if not np.allclose(X[-1,:],graph.x[i1].flat):
               print 'Error at to   %3d-%3d \t(%.2f)'\
                  %(i0,i1,norm(X[-1,:]-graph.x[i1].flat))
               errors += 1
     return errors


# --- MAIN -----------------------------------------------------------------------
# --- MAIN -----------------------------------------------------------------------
# --- MAIN -----------------------------------------------------------------------

LOAD_PRM     = True
EXTEND_PRM   = [ ]
LOAD_GRID    = True
SAMPLE_GRID  = False
REFINE_GRID  = [ ]
'''
EXTEND_PRM   = 0
LOAD_PRM     = True
SAMPLE_GRID  = False
REFINE_GRID  = 0
'''


RANDOM_SEED = 999 #int((time.time()%10)*1000)
print "Seed = %d" %  RANDOM_SEED
np .random.seed     (RANDOM_SEED)
random.seed         (RANDOM_SEED)

plt.ion()

env     = Bicopter(withDisplay=False)
NX      = 6
NQ      = 3
NV      = 3

acado = AcadoConnect(acadoBinDir+"connect_bicopter",
                     datadir=acadoTxtPath)
acado.NQ = NQ
acado.NV = NV
config(acado,'connect',env)


# --- PRM ---
# --- PRM ---
# --- PRM ---
prm = PRM(GraphBicopter(),
          sampler = env.reset,
          checker = lambda x:True,
          nearestNeighbor = NearestNeighbor(DistanceSO3([1,.1])),
          connect = ConnectAcado(acado))
prm = OptimalPRM.makeFromPRM(prm,acado=prm.connect.acado,stateDiff=BicopterStateDiff())

prm.graph.addNode(newConnex=True)
prm.graph.x[0] = zero(NX)

connect = prm.connect
nearest = prm.nearestNeighbor
graph   = prm.graph

if LOAD_PRM:
     prm.graph.load(dataRootPath)

if 1 in EXTEND_PRM:
     print '### Initial sampling of PRM',time.ctime()
     for i in range(2):
          prm(20,10,10,True)
          print 'Sleeping 1s ... it is time for a little CTRL-C ',time.ctime()
          time.sleep(1)
     graph.save(dataRootPath+'_100pts')

assert(checkPRM(prm.graph,True)==0)

if 2 in EXTEND_PRM:
     print '### Filling the prm with additional points at low speed.',time.ctime()
     env.vup[:] = .2
     env.vlow[:] = -.2
     for i in range(5):
          prm(10,50,50,False)
          print 'Sleeping 1s ... it is time for a little CTRL-C ',time.ctime()
          time.sleep(1)
     graph.save(dataRootPath+'_200pts')

assert(checkPRM(prm.graph,True)==0)

if 3 in EXTEND_PRM:
     print '### Filling the prm with additional points close to up equilibrium.',time.ctime()
     env.qup[:] = .2
     env.qlow[:] = -.2
     env.vup[:] = .5
     env.vlow[:] = -.5
     prevSize = len(graph.x)
     for i in range(5):
          prm(10,20,20,False)
          print 'Sleeping 1s ... it is time for a little CTRL-C ',time.ctime()
          time.sleep(1)
     graph.save(dataRootPath+'_400pts')

assert(checkPRM(prm.graph,True)==0)

if 4 in EXTEND_PRM:
     print '### Filling the prm with additional points close to joint limit. ',time.ctime()
     env.qlow = np.matrix([-5, -np.pi]).T
     env.qup  = np.matrix([ 5, -.6*np.pi]).T
     env.vlow[:] = -.5
     env.vup[:] = .5
     prevSize = len(graph.x)
     for i in range(5):
          prm(10,20,20,False)
          print 'Sleeping 1s ... it is time for a little CTRL-C ',time.ctime()
          time.sleep(1)

assert(checkPRM(prm.graph,True)==0)

if 5 in EXTEND_PRM:
     print '### Connect all points to zero (at least tries). ',time.ctime()
     prm.connectToZero(VERBOSE=True)
     assert(checkPRM(prm.graph,True)==0)

     print 'Connexify PRM',time.ctime()
     prm.connexifyPrm(NTRIAL=100,VERBOSE=True)
     prm.graph.save(dataRootPath)

assert(checkPRM(prm.graph,True)==0)

if 6 in EXTEND_PRM:
     print '### Densify PRM. ',time.ctime()
     config(acado,'traj')
     prm.densifyPrm(2000,VERBOSE=2)
     prm.graph.save(dataRootPath)

assert(checkPRM(prm.graph,True)==0)

print 'Done with the PRM. ',time.ctime()
# --- GRID ---
# --- GRID ---
# --- GRID ---

RANDOM_SEED = int((time.time()%10)*1000)
print "Seed = %d" %  RANDOM_SEED
np .random.seed     (RANDOM_SEED)
random.seed         (RANDOM_SEED)

#dataRootPath = dataRootPath + '/2dgrid'
grid = GridPolicy(prm)
EPS = 1e-3
grid.setGrid( np.concatenate([ env.qlow, zero(3) ]),
              np.concatenate([ env.qup , zero(3)+EPS ]), .1 )
#grid.setGrid( np.matrix([ -1., -1., 0,  0, 0, 0 ]).T,
#              np.matrix([  1.,  1., 0,  0, 0, 0 ]).T+EPS, .1 )

config(acado,'policy')
acado.setup_async(32,200)

if LOAD_GRID:
     grid.load(dataRootPath+'/grid_sampled.npy')

if SAMPLE_GRID:     
     print 'Sample the grid',time.ctime()
     grid.sample(subsample=1,verbose=True)
     np.save(dataRootPath+'/grid_sampled.npy',grid.data)
     print 'Sampling done',time.ctime()

if len(REFINE_GRID)>0: 
     config(acado,'refine')

if 1 in REFINE_GRID:
     print 'Fill the grid',time.ctime()
     grid.refineGrid(NNEIGHBOR=30,PERCENTAGE=.9, 
                     RANDQUEUE=[ i for i,d in enumerate(grid.data) if d.cost>100])
     grid.refineGrid(NNEIGHBOR=100,PERCENTAGE=.9, 
                     RANDQUEUE=[ i for i,d in enumerate(grid.data) if d.cost>100])
     np.save(dataRootPath+'/grid_filled.npy',grid.data)

if 2 in REFINE_GRID:
     print 'Refine the grid',time.ctime()
     grid.refineGrid(NTRIAL=500,NNEIGHBOR=20,PERCENTAGE=.9,verbose=True)
     np.save(dataRootPath+'/grid.npy',grid.data)

if 3 in REFINE_GRID:
     print 'Refine outliers in the grid',time.ctime()
     grid.refineGrid(500,NNEIGHBOR=30,PERCENTAGE=.8,verbose=True,
                     RANDQUEUE=[ i for i,d in enumerate(grid.data) if d.cost>3 ])
     np.save(dataRootPath+'/grid.npy',grid.data)

if 4 in REFINE_GRID:
     print 'Refine outliers in the grid',time.ctime()
     grid.refineGrid(5000,NNEIGHBOR=20,PERCENTAGE=1.1,verbose=True)
     np.save(dataRootPath+'/grid.npy',grid.data)

# --- MISC ---
# --- MISC ---
# --- MISC ---

fromcursor = FromCursor(prm,grid,env)
plt.ion()



# for i,c in graph.children.items():
#      graph.children[i] = list(set(c))
#      if len(c)!=len(set(c)): break
