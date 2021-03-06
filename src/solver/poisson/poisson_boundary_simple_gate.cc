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




#include "simulation_system.h"
#include "semiconductor_region.h"
#include "boundary_condition_simplegate.h"

using PhysicalUnit::kb;
using PhysicalUnit::e;



/*---------------------------------------------------------------------
 * set scaling constant
 */
void SimpleGateContactBC::Poissin_Fill_Value(Vec , Vec L)
{
  BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();
  for(; node_it!=end_it; ++node_it )
  {
    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    // search all the fvm_node which has *node_it as root node, these fvm_nodes have the same location in geometry,
    // but belong to different regions in logic.
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin(*node_it);
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end(*node_it);
    for(unsigned int i=0 ; rnode_it!=end_rnode_it; ++i, ++rnode_it  )
    {
      const SimulationRegion * region = ( *rnode_it ).second.first;
      switch ( region->type() )
      {
        case SemiconductorRegion :
        {
          const FVM_Node * fvm_node = ( *rnode_it ).second.second;
          VecSetValue(L, fvm_node->global_offset(), 1.0, INSERT_VALUES);
          break;
        }

        default: break;
      }
    }
  }
}


///////////////////////////////////////////////////////////////////////
//----------------Function and Jacobian evaluate---------------------//
///////////////////////////////////////////////////////////////////////



/*---------------------------------------------------------------------
 * build function and its jacobian for poisson solver
 */
void SimpleGateContactBC::Poissin_Function(PetscScalar * x, Vec f, InsertMode &add_value_flag)
{
  // Simple gate boundary condition is processed here

  // note, we will use ADD_VALUES to set values of vec f
  // if the previous operator is not ADD_VALUES, we should assembly the vec
  if( (add_value_flag != ADD_VALUES) && (add_value_flag != NOT_SET_VALUES) )
  {
    VecAssemblyBegin(f);
    VecAssemblyEnd(f);
  }

  const PetscScalar q = e*this->scalar("qf");            // surface change density
  const PetscScalar Thick = this->scalar("thickness");   // the thickness of gate oxide
  const PetscScalar eps_ox = this->scalar("eps");        // the permittivity of gate material
  const PetscScalar Work_Function = this->scalar("workfunction");

      BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();
  for(; node_it!=end_it; ++node_it )
  {
    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    // search all the fvm_node which has *node_it as root node
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin(*node_it);
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end(*node_it);

    // should clear all the rows related with this boundary condition
    for(; rnode_it!=end_rnode_it; ++rnode_it  )
    {

      const SimulationRegion * region = (*rnode_it).second.first;
      const FVM_Node * fvm_node = (*rnode_it).second.second;

      switch ( region->type() )
      {
      case SemiconductorRegion:
        {
          // psi of this node
          PetscScalar V = x[fvm_node->local_offset()];
          // Vapp
          PetscScalar Vapp = this->ext_circuit()->Vapp();
          PetscScalar S = fvm_node->outside_boundary_surface_area();
          PetscScalar dP = S*(eps_ox*(Vapp - Work_Function-V)/Thick + q);
          // set governing equation to function vector
          VecSetValue(f, fvm_node->global_offset(), dP, ADD_VALUES);
          break;
        }
      case VacuumRegion:
        break;

      default: genius_error(); //we should never reach here
      }
    }
  }

  // the last operator is ADD_VALUES
  add_value_flag = ADD_VALUES;
}




/*---------------------------------------------------------------------
 * build function and its jacobian for poisson solver
 */
void SimpleGateContactBC::Poissin_Jacobian(PetscScalar * x, Mat *jac, InsertMode &add_value_flag)
{
  // the Jacobian of simple gate boundary condition is processed here

  if( (add_value_flag != ADD_VALUES) && (add_value_flag != NOT_SET_VALUES) )
  {
    MatAssemblyBegin(*jac, MAT_FLUSH_ASSEMBLY);
    MatAssemblyEnd(*jac, MAT_FLUSH_ASSEMBLY);
  }

  const PetscScalar q = e*this->scalar("qf");            // surface change density
  const PetscScalar Thick = this->scalar("thickness");   // the thickness of gate oxide
  const PetscScalar eps_ox = this->scalar("eps");        // the permittivity of gate material
  const PetscScalar Work_Function = this->scalar("workfunction");

  // we use AD again. no matter it is overkill here.
  //the indepedent variable number, we only need 1 here.
  adtl::AutoDScalar::numdir=1;

  BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();
  for(node_it = nodes_begin(); node_it!=end_it; ++node_it )
  {

    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    // search all the fvm_node which has *node_it as root node
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin(*node_it);
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end(*node_it);

    // should clear all the rows related with this boundary condition
    for(; rnode_it!=end_rnode_it; ++rnode_it  )
    {

      const SimulationRegion * region = (*rnode_it).second.first;
      const FVM_Node * fvm_node = (*rnode_it).second.second;
      switch ( region->type() )
      {
      case SemiconductorRegion:
        {
          // psi of this node
          AutoDScalar V = x[fvm_node->local_offset()];  V.setADValue(0, 1.0);
          // Vapp
          PetscScalar Vapp = this->ext_circuit()->Vapp();
          PetscScalar S = fvm_node->outside_boundary_surface_area();
          AutoDScalar dP = S*(eps_ox*(Vapp - Work_Function-V)/Thick + q);
          //governing equation
          MatSetValue(*jac, fvm_node->global_offset(), fvm_node->global_offset(), dP.getADValue(0), ADD_VALUES);

          break;
        }
      case VacuumRegion:
        break;

      default: genius_error(); //we should never reach here
      }
    }
  }
  // the last operator is ADD_VALUES
  add_value_flag = ADD_VALUES;

}




/*---------------------------------------------------------------------
 * update electrode potential
 */
void SimpleGateContactBC::Poissin_Update_Solution(PetscScalar *)
{
  this->ext_circuit()->potential() = this->ext_circuit()->Vapp();
}
