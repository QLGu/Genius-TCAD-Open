/********************************************************************************/
/*     888888    888888888   88     888  88888   888      888    88888888       */
/*   8       8   8           8 8     8     8      8        8    8               */
/*  8            8           8  8    8     8      8        8    8               */
/*  8            888888888   8   8   8     8      8        8     8888888        */
/*  8      8888  8           8    8  8     8      8        8            8       */
/*   8       8   8           8     8 8     8      8        8            8       */
/*     888888    888888888  888     88   88888     88888888     88888888        */
/*                                                                              */
/*       A Three-Dimensional General Purpose Semiconductor Simulator.           */
/*                                                                              */
/*                                                                              */
/*  Copyright (C) 2007-2008                                                     */
/*  Cogenda Pte Ltd                                                             */
/*                                                                              */
/*  Please contact Cogenda Pte Ltd for license information                      */
/*                                                                              */
/*  Author: Gong Ding   gdiso@ustc.edu                                          */
/*                                                                              */
/********************************************************************************/

#include <string>
#include <cstdlib>
#include <ctime>
#include <iomanip>

#include "solver_base.h"
#include "probe_hook.h"
#include "parallel.h"


/*----------------------------------------------------------------------
 * constructor, open the file for writing
 */
ProbeHook::ProbeHook(SolverBase & solver, const std::string & name, void * param)
    : Hook(solver, name), _probe_file(SolverSpecify::out_prefix + ".probe")
{
  _p_solver = & solver;
  _p_fvm_node   = NULL;
  _min_loc = invalid_uint;

  const std::vector<Parser::Parameter> & parm_list = *((std::vector<Parser::Parameter> *)param);
  for(std::vector<Parser::Parameter>::const_iterator parm_it = parm_list.begin();
      parm_it != parm_list.end(); parm_it++)
  {
    if(parm_it->name() == "x" && parm_it->type() == Parser::REAL)
      _pp(0)=parm_it->get_real() * PhysicalUnit::um;
    if(parm_it->name() == "y" && parm_it->type() == Parser::REAL)
      _pp(1)=parm_it->get_real() * PhysicalUnit::um;
    if(parm_it->name() == "z" && parm_it->type() == Parser::REAL)
      _pp(2)=parm_it->get_real() * PhysicalUnit::um;
    if(parm_it->name() == "file" && parm_it->type() == Parser::STRING)
      _probe_file = parm_it->get_string();
  }


  if ( !Genius::processor_id() )
    _out.open(_probe_file.c_str());

}


/*----------------------------------------------------------------------
 * destructor, close file
 */
ProbeHook::~ProbeHook()
{
  if ( !Genius::processor_id() )
    _out.close();
}


/*----------------------------------------------------------------------
 *   This is executed before the initialization of the solver
 */
void ProbeHook::on_init()
{

  double min_dis = 1e100;
  for( unsigned int r=0; r<_p_solver->get_system().n_regions(); r++)
  {
    const SimulationRegion * region = _p_solver->get_system().region(r);

    SimulationRegion::const_processor_node_iterator node_it = region->on_processor_nodes_begin();
    SimulationRegion::const_processor_node_iterator node_it_end = region->on_processor_nodes_end();
    for(; node_it!=node_it_end; ++node_it)
    {
      const FVM_Node * fvm_node = *node_it;
      const Node * node = fvm_node->root_node();

      double dis = ((*node)-_pp).size();
      if(dis<min_dis)
      {
        min_dis = dis;
        _p_fvm_node = fvm_node;
      }
    }
  }

  // after this call, the _min_loc contains processor_id with minimal min_dis
  Parallel::min_loc(min_dis, _min_loc);

  double x,y,z;
  std::string region;
  std::vector<std::string> var_name;
  int n_var;

  x = (*_p_fvm_node->root_node())(0);
  y = (*_p_fvm_node->root_node())(1);
  z = (*_p_fvm_node->root_node())(2);
  region = _p_solver->get_system().region(_p_fvm_node->subdomain_id())->label();
  Parallel::broadcast(x, _min_loc);
  Parallel::broadcast(y, _min_loc);
  Parallel::broadcast(z, _min_loc);
  Parallel::broadcast(region, _min_loc);

  if (Genius::processor_id() == _min_loc)
  {
    const FVM_NodeData * node_data = _p_fvm_node->node_data();
    switch (node_data->type())
    {
      case FVM_NodeData::SemiconductorData:
        n_var = 3;
        var_name.push_back("psi [V]");
        var_name.push_back("n [cm^-3]");
        var_name.push_back("p [cm^-3]");
        break;
      case FVM_NodeData::InsulatorData:
      case FVM_NodeData::ConductorData:
      case FVM_NodeData::ResistanceData:
        n_var = 1;
        var_name.push_back("psi [V]");
        break;
      default:
        n_var = 0;
    }

  }

  Parallel::broadcast(n_var, _min_loc);
  var_name.resize(n_var);
  for(int i=0; i<n_var; i++)
    Parallel::broadcast(var_name[i], _min_loc);

  if ( !Genius::processor_id() )
  {
    time_t          _time;
    time(&_time);

    _out << "# Title: Gnuplot File Created by Genius TCAD Simulation" << std::endl;
    _out << "# Date: " << ctime(&_time) << std::endl;
    _out << "# Plotname: Probe"  << std::endl;
    _out << "# Node location (um): ";
    _out << "x=" << x/PhysicalUnit::um << "\ty=" << y/PhysicalUnit::um << "\tz=" << z/PhysicalUnit::um << std::endl;
    _out << "# Node in region: " << region << std::endl;
    _out << "# Variables: " << std::endl;
    int cCnt=0;

    // DC Sweep
    if( SolverSpecify::Type==SolverSpecify::DCSWEEP || SolverSpecify::Type==SolverSpecify::TRACE)
    {
      if ( SolverSpecify::Electrode_VScan.size() )
        for(unsigned int n=0; n<SolverSpecify::Electrode_VScan.size(); ++n)
          _out << '#' << std::setw(10) << ++cCnt << std::setw(20) << SolverSpecify::Electrode_VScan[n]  << " [V]"<< std::endl;

      if ( SolverSpecify::Electrode_IScan.size() )
        for(unsigned int n=0; n<SolverSpecify::Electrode_IScan.size(); ++n)
          _out << '#' << std::setw(10) << ++cCnt << std::setw(20) << SolverSpecify::Electrode_IScan[n]  << " [A]"<< std::endl;
    }

    // Transient
    if(SolverSpecify::Type==SolverSpecify::TRANSIENT)
    {
      _out << '#' << std::setw(10) << ++cCnt << std::setw(20) << "Time [s]"  << std::endl;
    }

    for(int i=0; i<n_var; i++)
    {
      _out << '#' << std::setw(10) << ++cCnt << std::setw(20) << var_name[i]  << std::endl;
    }
    _out << std::endl;
  }

}

/*----------------------------------------------------------------------
 *   This is executed previously to each solution step.
 */
void ProbeHook::pre_solve()
{}



/*----------------------------------------------------------------------
 *  This is executed after each solution step.
 */
void ProbeHook::post_solve()
{
  int n_var;
  std::vector<double> var;
  if (Genius::processor_id() == _min_loc)
  {
    const FVM_NodeData * node_data = _p_fvm_node->node_data();
    switch (node_data->type())
    {
      case FVM_NodeData::SemiconductorData:
        n_var = 3;
        var.push_back(node_data->psi()/PhysicalUnit::V);
        var.push_back(node_data->n()/std::pow(PhysicalUnit::cm, -3));
        var.push_back(node_data->p()/std::pow(PhysicalUnit::cm, -3));
        break;
      case FVM_NodeData::InsulatorData:
      case FVM_NodeData::ConductorData:
      case FVM_NodeData::ResistanceData:
        n_var = 1;
        var.push_back(node_data->psi()/PhysicalUnit::V);
        break;
      default:
        n_var = 0;
    }
  }

  Parallel::broadcast(n_var,_min_loc);
  if(n_var)
    Parallel::broadcast(var,_min_loc);

  if ( !Genius::processor_id() )
  {
    // set the float number precision
    _out.precision(6);

    // set output width and format
    _out<< std::scientific << std::right;

    _out<<' ';

    // DC Sweep
    if( SolverSpecify::Type==SolverSpecify::DCSWEEP || SolverSpecify::Type==SolverSpecify::TRACE )
    {
      const BoundaryConditionCollector * bcs = this->get_solver().get_system().get_bcs();

      if ( SolverSpecify::Electrode_VScan.size() )
        for(unsigned int n=0; n<SolverSpecify::Electrode_VScan.size(); ++n)
      {
        const BoundaryCondition * bc = bcs->get_bc(SolverSpecify::Electrode_VScan[n]);
        _out << std::setw(20) << bc->ext_circuit()->Vapp()/PhysicalUnit::V;
      }

      if ( SolverSpecify::Electrode_IScan.size() )
        for(unsigned int n=0; n<SolverSpecify::Electrode_IScan.size(); ++n)
      {
        const BoundaryCondition * bc = bcs->get_bc(SolverSpecify::Electrode_IScan[n]);
        _out << std::setw(20) << bc->ext_circuit()->Iapp()/PhysicalUnit::A;
      }
    }

    if(SolverSpecify::Type==SolverSpecify::TRANSIENT)
    {
      _out << std::setw(15) << SolverSpecify::clock/PhysicalUnit::s;
    }

    for(unsigned int i=0; i<var.size(); i++)
      _out << std::setw(15) << var[i];

    _out << std::endl;
  }
}



/*----------------------------------------------------------------------
 *  This is executed after each (nonlinear) iteration
 */
void ProbeHook::post_iteration()
{}



/*----------------------------------------------------------------------
 * This is executed after the finalization of the solver
 */
void ProbeHook::on_close()
{}


#ifdef DLLHOOK

// dll interface
extern "C"
{
  Hook* get_hook (SolverBase & solver, const std::string & name, void * fun_data)
  {
    return new ProbeHook(solver, name, fun_data );
  }

}

#endif

