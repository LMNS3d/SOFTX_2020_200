/* ---------------------------------------------------------------------
 *
 * Copyright (C) 2019 -  by the Lethe authors
 *
 * This file is part of the Lethe library
 *
 * The Lethe library is free software; you can use it, redistribute
 * it, and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * The full text of the license can be found in the file LICENSE at
 * the top level of the Lethe distribution.
 *
 * ---------------------------------------------------------------------

 *
 * Author: Bruno Blais, Polytechnique Montreal, 2019 -
 */

#ifndef LETHE_NAVIERSTOKESSOLVERPARAMETERS_H
#define LETHE_NAVIERSTOKESSOLVERPARAMETERS_H

#include <core/parameters.h>
#include <core/simulation_control.h>

#include "analytical_solutions.h"
#include "boundary_conditions.h"
#include "initial_conditions.h"
#include "manifolds.h"
#include "source_terms.h"

template <int dim>
class NavierStokesSolverParameters
{
public:
  Parameters::Testing                           test;
  Parameters::LinearSolver                      linearSolver;
  Parameters::NonLinearSolver                   nonLinearSolver;
  Parameters::MeshAdaptation                    meshAdaptation;
  Parameters::Mesh                              mesh;
  Parameters::PhysicalProperties                physicalProperties;
  Parameters::Timer                             timer;
  Parameters::FEM                               femParameters;
  Parameters::Forces                            forcesParameters;
  Parameters::PostProcessing                    postProcessingParameters;
  Parameters::Restart                           restartParameters;
  Parameters::Manifolds                         manifoldsParameters;
  BoundaryConditions::NSBoundaryConditions<dim> boundaryConditions;

  std::shared_ptr<Parameters::InitialConditions<dim>> initialCondition;
  std::shared_ptr<AnalyticalSolutions::NSAnalyticalSolution<dim>>
                                                  analyticalSolution;
  std::shared_ptr<SourceTerms::NSSourceTerm<dim>> sourceTerm;

  SimulationControl simulationControl;

  NavierStokesSolverParameters()
    : initialCondition(std::make_shared<Parameters::InitialConditions<dim>>())
    , analyticalSolution(
        std::make_shared<AnalyticalSolutions::NSAnalyticalSolution<dim>>())
    , sourceTerm(std::make_shared<SourceTerms::NSSourceTerm<dim>>())
  {}

  void
  declare(ParameterHandler &prm)
  {
    Parameters::SimulationControl::declare_parameters(prm);
    Parameters::PhysicalProperties::declare_parameters(prm);
    Parameters::Mesh::declare_parameters(prm);
    Parameters::Restart::declare_parameters(prm);
    boundaryConditions.declare_parameters(prm);


    initialCondition->declare_parameters(prm);

    Parameters::FEM::declare_parameters(prm);
    Parameters::Timer::declare_parameters(prm);
    Parameters::Forces::declare_parameters(prm);
    Parameters::MeshAdaptation::declare_parameters(prm);
    Parameters::NonLinearSolver::declare_parameters(prm);
    Parameters::LinearSolver::declare_parameters(prm);
    Parameters::PostProcessing::declare_parameters(prm);
    manifoldsParameters.declare_parameters(prm);

    analyticalSolution->declare_parameters(prm);

    sourceTerm->declare_parameters(prm);
    Parameters::Testing::declare_parameters(prm);
  }

  void
  parse(ParameterHandler &prm)
  {
    test.parse_parameters(prm);
    linearSolver.parse_parameters(prm);
    nonLinearSolver.parse_parameters(prm);
    meshAdaptation.parse_parameters(prm);
    mesh.parse_parameters(prm);
    physicalProperties.parse_parameters(prm);
    timer.parse_parameters(prm);
    femParameters.parse_parameters(prm);
    forcesParameters.parse_parameters(prm);
    postProcessingParameters.parse_parameters(prm);
    restartParameters.parse_parameters(prm);
    boundaryConditions.parse_parameters(prm);
    manifoldsParameters.parse_parameters(prm);
    initialCondition->parse_parameters(prm);
    analyticalSolution->parse_parameters(prm);
    sourceTerm->parse_parameters(prm);
    simulationControl.initialize(prm);
  }

  void
  parse(boost::property_tree::ptree &root)
  {
    test.parse_parameters(root);
    linearSolver.parse_parameters(root);
    nonLinearSolver.parse_parameters(root);
    meshAdaptation.parse_parameters(root);
    mesh.parse_parameters(root);
    physicalProperties.parse_parameters(root);
    timer.parse_parameters(root);
    femParameters.parse_parameters(root);
    forcesParameters.parse_parameters(root);
    postProcessingParameters.parse_parameters(root);
    restartParameters.parse_parameters(root);
    boundaryConditions.parse_parameters(root);
    // manifoldsParameters.parse_parameters(root);
    // initialCondition->parse_parameters(root);
    // analyticalSolution->parse_parameters(root);
    // sourceTerm->parse_parameters(root);
    // simulationControl.initialize(root);
  }
};

#endif
