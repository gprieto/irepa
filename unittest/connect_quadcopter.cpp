/*
 * Connect in minimum time a starting position to a final state.
 */

#include <acado_toolkit.hpp>
#include "pycado/utils.hpp"

struct OptionsQuadcopter : public OptionsOCP
{
  virtual void addExtraOptions()
  {
    desc.add_options()
      ("maxAngle",       po::value<double>()->default_value(1.5707963267948967),         "Max angles for roll and pitch");
    desc.add_options()
      ("maxAngleSing",   po::value<double>()->default_value(1.5707963268),         "Max angle for the singular axis i.e pitch");
  }

  const double & maxAngle()        { return vm["maxAngle"].as<double>(); }
  const double & maxAngleSing()        { return vm["maxAngleSing"].as<double>(); }
};

void outputControlAndStateWithoutLagrangeTerm( ACADO::OptimizationAlgorithm  & algorithm, OptionsOCP & opts )
{
  USING_NAMESPACE_ACADO;
  if( opts.withOutputControl() )
    {
      VariablesGrid Us;
      algorithm.getControls( Us );
      std::ofstream of(opts.outputControlFile().c_str());
      Us.print( of,"","","\n",10,10,"\t","\n");
    }

  if( opts.withOutputState() )
    {
      VariablesGrid Xs_full;
      algorithm.getDifferentialStates( Xs_full );
      VariablesGrid Xs(10, Xs_full.getTimePoints());
      int idex = 0;
      for (int i=0; i<12; i++) //Do not copy last variable = LagrangeTerm
      {
          if (i==5 || i==11) continue; //Do not copy yaw and r
          for (int j=0; j<Xs_full.getTimePoints().getNumPoints(); j++)
          {
            Xs(j, idex) = Xs_full(j, i);
          }
          idex++;
      }
      std::ofstream of(opts.outputStateFile().c_str());
      Xs.print( of,"","","\n",10,10,"\t","\n");
    }
}

void initControlAndStateQuadcopter( ACADO::OptimizationAlgorithm  & algorithm, OptionsOCP & opts )
{
    USING_NAMESPACE_ACADO;
    if(opts.withGuessControl()) {  algorithm.initializeControls          (opts.guessControl());}
    if(opts.withGuessState  ())
    {
        VariablesGrid Xs(opts.guessState());
        VariablesGrid Xs_full(13, Xs.getTimePoints());
        Xs_full.setAll(0);
        for (int i=0; i<Xs_full.getTimePoints().getNumPoints(); i++)
        {
            Xs_full(i,0) = Xs(i,0);
            Xs_full(i,1) = Xs(i,1);
            Xs_full(i,2) = Xs(i,2);
            Xs_full(i,3) = Xs(i,3);
            Xs_full(i,4) = Xs(i,4);
            Xs_full(i,6) = Xs(i,5);
            Xs_full(i,7) = Xs(i,6);
            Xs_full(i,8) = Xs(i,7);
            Xs_full(i,9) = Xs(i,8);
            Xs_full(i,10) = Xs(i,9);

        }
        algorithm.initializeDifferentialStates(Xs_full);
    }
}



int main(int argc, const char ** argv )
{
  USING_NAMESPACE_ACADO;
  Timer timer;



  /* --- OPTIONS ----------------------------------------------------------------------------- */
  /* --- OPTIONS ----------------------------------------------------------------------------- */
  /* --- OPTIONS ----------------------------------------------------------------------------- */
  OptionsQuadcopter opts; opts.parse(argc,argv);
  opts.NQ = 5; opts.NV = 5; opts.NU = 4;
  assert( opts.friction().size() == 2);
  assert( opts.umax()    .size() == 4);

  opts.displayBoundaryConditions(std::cout);

  /* --- OCP ----------------------------------------------------------------------------- */
  /* --- OCP ----------------------------------------------------------------------------- */
  /* --- OCP ----------------------------------------------------------------------------- */

  /// Pendulum hyperparameters

  const double l     =  .5 ;    // lever arm
  const double m     = 2.5 ;    // mass
  const double Ixx   = 1.  ;    // roll inertia XX
  const double Iyy   = 1.  ;    // pitch inertia YY
  const double Izz   = 1.6 ;    // yaw inertia ZZ
  const double Ct    = 0.1 ;    // Coeff torque/force of each propeller
  const double g     = 9.81;    // gravity constant
  const double DT    = opts.T()/opts.steps(); // integration time
  const double umax1 = opts.umax()[0], umax2 = opts.umax()[1], umax3 = opts.umax()[2], umax4 = opts.umax()[3];


  //WARNING: The angles roll-pitch-yaw are considered in the inverse order of the usual convention RPY
  //         i.e YAW then PITCH then ROLL. This means that ROLL always affect the world's Y axis, no matter the yaw.
  //
  // p, q, r follow the usual convention i.e p is the rotational speed around the local X axis,
  //                                         q is the rotational speed around the local Y axis,
  //                                         r is the rotational speed around the local Z axis,

  DifferentialState        qx, qy, qz, roll, pitch, vx, vy, vz, p, q;
  Control                  f1,f2,f3,f4;
  Parameter                T;
  DifferentialEquation     f( 0.0, T );   // the differential equation

  //IntermediateState dyaw = cos(yaw)*tan(pitch)*p+sin(yaw)*tan(pitch)*q+r;
  IntermediateState dpitch = q;
  IntermediateState droll = 1/cos(pitch)*p;
  IntermediateState fTot = f1+f2+f3+f4;

  //  --- SETUP OCP -----------------------
  OCP ocp( 0.0, T, opts.steps() );                        // time horizon of the OCP: [0,T]

  ocp.minimizeMayerTerm( T );
  //ocp.minimizeLagrangeTerm( 1000*yaw*yaw + 100*dyaw*dyaw );      //WARNING: LagrangeTerm is added as a additionnal state...

  f << dot(qx) == vx;
  f << dot(qy) == vy;
  f << dot(qz) == vz;
  f << dot(vx) == fTot*sin(pitch)/m;                //(f1+f2+f3+f4)*sin(pitch)/m;
  f << dot(vy) == fTot*sin(roll)*cos(pitch)/m;      //(f1+f2+f3+f4)*sin(roll)*cos(pitch)/m;
  f << dot(vz) == fTot*cos(roll)*cos(pitch)/m - g;  //(f1+f2+f3+f4)*cos(roll)*cos(pitch)/m - g;
  f << dot(pitch) == dpitch;                        //sin(yaw)*p+cos(yaw)*q;
  f << dot(roll) == droll;                          //cos(yaw)/cos(pitch)*p-sin(yaw)/cos(pitch)*q;
  f << dot(p) == (l*(f1-f2)+(Iyy-Izz)*q)/Ixx;
  f << dot(q) == (l*(f4-f3)+(Izz-Ixx)*p)/Iyy;
  //f << dot(r) == Ct*(f1+f2-f3-f4)/Izz;

  ocp.subjectTo( f );

  //FIXED CONSTRAINTS:

  //Controls
  ocp.subjectTo( 0.  <= f1 <=  umax1   );
  ocp.subjectTo( 0.  <= f2 <=  umax2   );
  ocp.subjectTo( 0.  <= f3 <=  umax3   );
  ocp.subjectTo( 0.  <= f4 <=  umax4   );

  //Avoid Flipping roll
  ocp.subjectTo( -opts.maxAngle()  <= roll <=  opts.maxAngle()   );

  //Avoid Flipping or Singularity pitch
  if (opts.maxAngle() > opts.maxAngleSing())
  {
      //Avoid Singularity
      ocp.subjectTo( -opts.maxAngleSing()  <= pitch <=  opts.maxAngleSing()  );
  }
  else
  {
      ocp.subjectTo( -opts.maxAngle()  <= pitch <=  opts.maxAngle()   );
  }

  //Out[64]: '/home/nmansard/src/pinocchio/pycado/build/unittest/connect_quadcopter --icontrol=/media/ramdisk/acado/process_3092/guess.clt --umax=25.00 25.00 25.00 25.00 --ocontrol=/media/ramdisk/acado/process_3092/mpc.ctl --ostate=/media/ramdisk/acado/process_3092/mpc.stx --plot --Tmin=0.001 --iter=80 --maxAngle=1.57079632679 --acadoKKT=0.0001 --istate=/media/ramdisk/acado/process_3092/guess.stx --steps=20 --printlevel=2 --oparam=/media/ramdisk/acado/process_3092/mpc.prm --initpos=1.33210438965793986910 0.13778088163072554906 0.64432925172179356110 0.28525541992832570415 -0.28881288677532768183 --initvel=-0.90930527914862102623 0.49287897362600802120 -0.70437751649159840994 0.11160754548927977192 0.76089588652849715622 --finalpos=0.57736691356793989893 0.37999095584072550835 1.16241229530179346341 0.94238062408832568018 0.56453766202467225988 --finalvel=-1.08922746119862079439 -0.28073074285399196581 0.41088645728840161864 -0.31934786656072022826 0.91267344654849724694 --horizon=1.7155258290'

  //Time boundaries
  ocp.subjectTo( opts.Tmin()  <= T  <= opts.Tmax()  );

  //PRM CONSTRAINTS:
  //Start
  ocp.subjectTo( AT_START,  qx  ==  opts.configInit ()[0] );
  ocp.subjectTo( AT_START,  vx  ==  opts.velInit    ()[0] );

  ocp.subjectTo( AT_START,  qy  ==  opts.configInit ()[1] );
  ocp.subjectTo( AT_START,  vy  ==  opts.velInit    ()[1] );

  ocp.subjectTo( AT_START,  qz  ==  opts.configInit ()[2] );
  ocp.subjectTo( AT_START,  vz  ==  opts.velInit    ()[2] );

//  ocp.subjectTo( AT_START,  yaw ==  opts.configInit ()[3] );
  ocp.subjectTo( AT_START,  roll ==  opts.configInit ()[3] );
  ocp.subjectTo( AT_START, pitch ==  opts.configInit ()[4] );

//  ocp.subjectTo( AT_START,  dyaw ==  opts.configInit ()[3] );
//  ocp.subjectTo( AT_START,dpitch ==  opts.velInit    ()[3] );
//  ocp.subjectTo( AT_START, droll ==  opts.velInit    ()[4] );

  ocp.subjectTo( AT_START,  p ==  opts.velInit    ()[3] );
  ocp.subjectTo( AT_START,  q ==  opts.velInit    ()[4] );
//  ocp.subjectTo( AT_START,  r ==  opts.velInit    ()[5] );

  //End
  ocp.subjectTo( AT_END  ,  qx  ==  opts.configFinal()[0] );
  ocp.subjectTo( AT_END  ,  vx  ==  opts.velFinal   ()[0] );

  ocp.subjectTo( AT_END  ,  qy  ==  opts.configFinal()[1] );
  ocp.subjectTo( AT_END  ,  vy  ==  opts.velFinal   ()[1] );

  ocp.subjectTo( AT_END  ,  qz  ==  opts.configFinal()[2] );
  ocp.subjectTo( AT_END  ,  vz  ==  opts.velFinal   ()[2] );

//  ocp.subjectTo( AT_END,  yaw ==  opts.configFinal ()[3] );
  ocp.subjectTo( AT_END,   roll ==  opts.configFinal ()[3] );
  ocp.subjectTo( AT_END,  pitch ==  opts.configFinal ()[4] );

//  ocp.subjectTo( AT_END,  dyaw ==  opts.configFinal ()[3] );
//  ocp.subjectTo( AT_END, dpitch ==  opts.velFinal    ()[3] );
//  ocp.subjectTo( AT_END,  droll ==  opts.velFinal    ()[4] );

  ocp.subjectTo( AT_END,  p ==  opts.velFinal    ()[3] );
  ocp.subjectTo( AT_END,  q ==  opts.velFinal    ()[4] );
//  ocp.subjectTo( AT_END,  r ==  opts.velFinal    ()[5] );


  //  --- SETUP SOLVER --------------------

  OptimizationAlgorithm algorithm(ocp);

  // const std::vector<std::string> plotNames = {"X", "Y", "Z", "roll", "pitch"};
  // std::vector<ACADO::Expression> plotExpr = {qx, qy, qz, roll, pitch};
  const std::vector<std::string> plotNames = {"X", "Y", "Z", "roll"};
  std::vector<ACADO::Expression> plotExpr = {qx, qy, qz, roll};

  setupPlots(algorithm,opts,plotExpr,plotNames);

  //initControlAndStateQuadcopter(algorithm,opts);
  initControlAndState(algorithm,opts);

  //Full static inital guess
//  Grid timeGrid(0.0,1.,opts.steps()+1);
//  VariablesGrid x_init(10, timeGrid);
//  x_init.setAll(0);
//  VariablesGrid u_init(4, timeGrid);
//  VariablesGrid param(1,timeGrid);
//  param.setAll(15);
//  for (int i = 0 ; i<opts.steps()+1 ; i++ ) {
//      u_init(i,0) = 6.13;
//      u_init(i,1) = 6.13;
//      u_init(i,2) = 6.13;
//      u_init(i,3) = 6.13;
//  }
//  algorithm.initializeDifferentialStates(x_init);
//  algorithm.initializeControls(u_init);
//  algorithm.initializeParameters(param);

  initHorizon(algorithm,opts);
  initAlgorithmStandardParameters(algorithm,opts);

  returnValue retval = algorithm.solve();

  outputControlAndState(algorithm,opts);
  //outputControlAndStateWithoutLagrangeTerm(algorithm,opts);
  outputParameters(algorithm,opts);

  //  --- RETURN --------------------------
  std::cout << "###### Return["<<int(retval)<<"] JobID=" << opts.jobid() << timer << std::endl;
  return (int)retval;
}

/*
/home/nmansard/src/pinocchio/pycado/build/unittest/connect_quadcopter --Tmax=20.0  --umax=12.50 12.50 12.50 12.50 --ocontrol=/media/ramdisk/acado/process_7051/mpc.ctl --ostate=/media/ramdisk/acado/process_7051/mpc.stx --plot --Tmin=0.001  --maxAngle=1.57079632679 --acadoKKT=0.0001  --steps=20 --horizon=8.3508798106e-01 --printlevel=2 --oparam=/media/ramdisk/acado/process_7051/mpc.prm --initpos=0.1 0 0 0 0  --initvel=0 0 0 0 0  --finalpos=0 0 0 0 0  --finalvel=0 0 0 0 0  --iter=2000 --istate=/media/ramdisk/acado/process_7051/copy.stx --icontrol=/media/ramdisk/acado/process_7051/copy.ctl
*/
