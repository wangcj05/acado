/*
 *    This file is part of ACADO Toolkit.
 *
 *    ACADO Toolkit -- A Toolkit for Automatic Control and Dynamic Optimization.
 *    Copyright (C) 2008-2014 by Boris Houska, Hans Joachim Ferreau,
 *    Milan Vukov, Rien Quirynen, KU Leuven.
 *    Developed within the Optimization in Engineering Center (OPTEC)
 *    under supervision of Moritz Diehl. All rights reserved.
 *
 *    ACADO Toolkit is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    ACADO Toolkit is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with ACADO Toolkit; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 *    \file src/code_generation/export_gauss_newton_block_forces.cpp
 *    \author Rien Quirynen
 *    \date 2014
 */

#include <acado/code_generation/export_gauss_newton_block_forces.hpp>

#include <acado/code_generation/templates/templates.hpp>

using namespace std;

BEGIN_NAMESPACE_ACADO

ExportGaussNewtonBlockForces::ExportGaussNewtonBlockForces(	UserInteraction* _userInteraction,
											const std::string& _commonHeaderName
											) : ExportGaussNewtonBlockCN2( _userInteraction,_commonHeaderName )
{
	qpObjPrefix = "acadoForces";
	qpModuleName = "forces";
}

returnValue ExportGaussNewtonBlockForces::setup( )
{
	returnValue status = ExportGaussNewtonBlockCN2::setup();
	if( status != SUCCESSFUL_RETURN ) return status;

	LOG( LVL_DEBUG ) << "Solver: setup extra initialization... " << endl;
	// Add QP initialization call to the initialization
	ExportFunction initializeForces( "initializeForces" );
	initialize.addFunctionCall( initializeForces );
	LOG( LVL_DEBUG ) << "done!" << endl;

	return SUCCESSFUL_RETURN;
}

returnValue ExportGaussNewtonBlockForces::getCode(	ExportStatementBlock& code
											)
{
	setupQPInterface();
	code.addStatement( *qpInterface );

	code.addFunction( cleanup );
	code.addFunction( shiftQpData );

	code.addFunction( evaluateConstraints );

	return ExportGaussNewtonCN2::getCode( code );
}

returnValue ExportGaussNewtonBlockForces::setupCondensing( void )
{
	returnValue status = ExportGaussNewtonBlockCN2::setupCondensing();
	if( status != SUCCESSFUL_RETURN ) return status;

	// TODO: REWRITE EXPAND ROUTINE + SET HESSIAN AND GRADIENT INFORMATION

	LOG( LVL_DEBUG ) << "Setup condensing: rewrite expand routine" << endl;
	expand.setup( "expand", blockI );

	//	if (performFullCondensing() == true)
	//	{
	//		expand.addStatement( u.makeRowVector() += xVars.getTranspose() );
	//	}
	//	else
	//	{
	for (unsigned i = 0; i < getBlockSize(); ++i ) {
		expand.addStatement( (u.getRow(blockI*getBlockSize()+i)).getTranspose() += xVars.getRows(blockI*getNumBlockVariables()+NX+i*NU, blockI*getNumBlockVariables()+NX+(i+1)*NU) );
	}
	//	}

	//	if( performFullCondensing() == true ) {
	//		expand.addStatement( sbar.getRows(0, NX) == Dx0 );
	//	}
	//	else {
	expand.addStatement( sbar.getRows(0, NX) == xVars.getRows(blockI*getNumBlockVariables(), blockI*getNumBlockVariables()+NX) );
	expand.addStatement( (x.getRow(blockI*getBlockSize())).getTranspose() += sbar.getRows(0, NX) );
	//	}
	if( getBlockSize() > 1 ) {
		expand.addStatement( sbar.getRows(NX, getBlockSize()*NX) == d.getRows(blockI*getBlockSize()*NX,(blockI+1)*getBlockSize()*NX-NX) );
	}

	for (unsigned row = 0; row < getBlockSize()-1; ++row ) {
		expand.addFunctionCall(
				expansionStep, evGx.getAddress((blockI*getBlockSize()+row) * NX), evGu.getAddress((blockI*getBlockSize()+row) * NX),
				xVars.getAddress(blockI*getNumBlockVariables()+offset + row * NU), sbar.getAddress(row * NX),
				sbar.getAddress((row + 1) * NX)
		);
		expand.addStatement( (x.getRow(blockI*getBlockSize()+row+1)).getTranspose() += sbar.getRows((row+1)*NX, (row+2)*NX) );
	}

	// !! Calculation of multipliers: !!
	int hessianApproximation;
	get( HESSIAN_APPROXIMATION, hessianApproximation );
	bool secondOrder = ((HessianApproximationMode)hessianApproximation == EXACT_HESSIAN);
	if( secondOrder ) {
		return ACADOERROR( RET_NOT_IMPLEMENTED_YET );
		//	mu_N = lambda_N + q_N + Q_N^T * Ds_N  --> wrong in Joel's paper !!
		//		for i = N - 1: 1
		//			mu_k = Q_k^T * Ds_k + A_k^T * mu_{k + 1} + S_k * Du_k + q_k

		for (uint j = 0; j < NX; j++ ) {
			uint item = N*NX+j;
			uint IdxF = std::find(xBoundsIdx.begin(), xBoundsIdx.end(), item) - xBoundsIdx.begin();
			if( IdxF != xBoundsIdx.size() ) { // INDEX FOUND
				expand.addStatement( mu.getSubMatrix(N-1,N,j,j+1) == yVars.getRow(getNumQPvars()+IdxF) );
			}
			else { // INDEX NOT FOUND
				expand.addStatement( mu.getSubMatrix(N-1,N,j,j+1) == 0.0 );
			}
		}
		expand.addStatement( mu.getRow(N-1) += sbar.getRows(N*NX,(N+1)*NX).getTranspose()*QN1 );
		expand.addStatement( mu.getRow(N-1) += QDy.getRows(N*NX,(N+1)*NX).getTranspose() );
		for (int i = N - 1; i >= 1; i--) {
			for (uint j = 0; j < NX; j++ ) {
				uint item = i*NX+j;
				uint IdxF = std::find(xBoundsIdx.begin(), xBoundsIdx.end(), item) - xBoundsIdx.begin();
				if( IdxF != xBoundsIdx.size() ) { // INDEX FOUND
					expand.addStatement( mu.getSubMatrix(i-1,i,j,j+1) == yVars.getRow(getNumQPvars()+IdxF) );
				}
				else { // INDEX NOT FOUND
					expand.addStatement( mu.getSubMatrix(i-1,i,j,j+1) == 0.0 );
				}
			}
			expand.addFunctionCall(
					expansionStep2, QDy.getAddress(i*NX), Q1.getAddress(i * NX), sbar.getAddress(i*NX),
					S1.getAddress(i * NX), xVars.getAddress(offset + i * NU), evGx.getAddress(i * NX),
					mu.getAddress(i-1), mu.getAddress(i) );
		}
	}

	return SUCCESSFUL_RETURN;
}

returnValue ExportGaussNewtonBlockForces::setupConstraintsEvaluation( void )
{
	returnValue status = ExportGaussNewtonBlockCN2::setupConstraintsEvaluation();
	if( status != SUCCESSFUL_RETURN ) return status;

	////////////////////////////////////////////////////////////////////////////
	//
	// Setup evaluation of box constraints on states and controls
	//
	////////////////////////////////////////////////////////////////////////////

//	conLB.clear();
//	conLB.resize(N + 1);
//
//	conUB.clear();
//	conUB.resize(N + 1);
//
//	conLBIndices.clear();
//	conLBIndices.resize(N + 1);
//
//	conUBIndices.clear();
//	conUBIndices.resize(N + 1);
//
//	conABIndices.clear();
//	conABIndices.resize(N + 1);
//
//	conLBValues.clear();
//	conLBValues.resize(N + 1);
//
//	conUBValues.clear();
//	conUBValues.resize(N + 1);
//
//	DVector lbTmp, ubTmp;
//
//	//
//	// Stack state constraints
//	//
//	for (unsigned i = 0; i < xBounds.getNumPoints(); ++i)
//	{
//		lbTmp = xBounds.getLowerBounds( i );
//		ubTmp = xBounds.getUpperBounds( i );
//
//		if (isFinite( lbTmp ) == false && isFinite( ubTmp ) == false)
//			continue;
//
//		for (unsigned j = 0; j < lbTmp.getDim(); ++j)
//		{
//			if (acadoIsFinite( lbTmp( j ) ) == true)
//			{
//				conLBIndices[ i ].push_back( j );
//				conLBValues[ i ].push_back( lbTmp( j ) );
//				numLB++;
//			}
//
//			if (acadoIsFinite( ubTmp( j ) ) == true)
//			{
//				conUBIndices[ i ].push_back( j );
//				conUBValues[ i ].push_back( ubTmp( j ) );
//				numUB++;
//			}
//		}
//	}
//
//	//
//	// Stack control constraints
//	//
//	for (unsigned i = 0; i < uBounds.getNumPoints() && i < N; ++i)
//	{
//		lbTmp = uBounds.getLowerBounds( i );
//		ubTmp = uBounds.getUpperBounds( i );
//
//		if (isFinite( lbTmp ) == false && isFinite( ubTmp ) == false)
//			continue;
//
//		for (unsigned j = 0; j < lbTmp.getDim(); ++j)
//		{
//			if (acadoIsFinite( lbTmp( j ) ) == true)
//			{
//				conLBIndices[ i ].push_back(NX + j);
//				conLBValues[ i ].push_back( lbTmp( j ) );
//				numLB++;
//			}
//
//			if (acadoIsFinite( ubTmp( j ) ) == true)
//			{
//				conUBIndices[ i ].push_back(NX + j);
//				conUBValues[ i ].push_back( ubTmp( j ) );
//				numUB++;
//			}
//		}
//	}
//
//	//
//	// Setup variables
//	//
//	for (unsigned i = 0; i < N + 1; ++i)
//	{
//		conLB[ i ].setup(string("lb") + toString(i + 1), conLBIndices[ i ].size(), 1, REAL, FORCES_PARAMS, false, qpObjPrefix);
//		conUB[ i ].setup(string("ub") + toString(i + 1), conUBIndices[ i ].size(), 1, REAL, FORCES_PARAMS, false, qpObjPrefix);
//	}
//
//	int hardcodeConstraintValues;
//	get(CG_HARDCODE_CONSTRAINT_VALUES, hardcodeConstraintValues);
//	uint numBounds = numLB+numUB;
//	if (!hardcodeConstraintValues && numBounds > 0)
//	{
//		lbValues.setup("lbValues", numLB, 1, REAL, ACADO_VARIABLES);
//		lbValues.setDoc( "Lower bounds values." );
//		ubValues.setup("ubValues", numUB, 1, REAL, ACADO_VARIABLES);
//		ubValues.setDoc( "Upper bounds values." );
//	}
//
//	evaluateConstraints.setup("evaluateConstraints");
//
//	//
//	// Export evaluation of simple box constraints
//	//
//	uint indexB = 0;
//	for (unsigned i = 0; i < N + 1; ++i) {
//		for (unsigned j = 0; j < conLBIndices[ i ].size(); ++j)
//		{
//			if( hardcodeConstraintValues ) {
//				evaluateConstraints << conLB[ i ].getFullName() << "[ " << toString(j) << " ]" << " = " << toString(conLBValues[ i ][ j ]) << " - ";
//			}
//			else {
//				evaluateConstraints << conLB[ i ].getFullName() << "[ " << toString(j) << " ]" << " = " << lbValues.get( indexB,0 ) << " - ";
//			}
//			indexB++;
//
//			if (conLBIndices[ i ][ j ] < NX)
//				evaluateConstraints << x.getFullName() << "[ " << toString(i * NX + conLBIndices[ i ][ j ]) << " ];\n";
//			else
//				evaluateConstraints << u.getFullName() << "[ " << toString(i * NU + conLBIndices[ i ][ j ] - NX) << " ];\n";
//		}
//	}
//	evaluateConstraints.addLinebreak();
//
//	indexB = 0;
//	for (unsigned i = 0; i < N + 1; ++i)
//		for (unsigned j = 0; j < conUBIndices[ i ].size(); ++j)
//		{
//			if( hardcodeConstraintValues ) {
//				evaluateConstraints << conUB[ i ].getFullName() << "[ " << toString(j) << " ]" << " = " << toString(conUBValues[ i ][ j ]) << " - ";
//			}
//			else {
//				evaluateConstraints << conUB[ i ].getFullName() << "[ " << toString(j) << " ]" << " = " << ubValues.get( indexB,0 ) << " - ";
//			}
//			indexB++;
//
//			if (conUBIndices[ i ][ j ] < NX)
//				evaluateConstraints << x.getFullName() << "[ " << toString(i * NX + conUBIndices[ i ][ j ]) << " ];\n";
//			else
//				evaluateConstraints << u.getFullName() << "[ " << toString(i * NU + conUBIndices[ i ][ j ] - NX) << " ];\n";
//		}
//	evaluateConstraints.addLinebreak();
//
//	////////////////////////////////////////////////////////////////////////////
//	//
//	// Evaluation of the equality constraints
//	//  - system dynamics only
//	//
//	////////////////////////////////////////////////////////////////////////////
//
//	conC.clear();
//	conC.resize( N );
//
//	// XXX FORCES works with column major format
//	//	if (initialStateFixed() == true)
//	//		conC[ 0 ].setup("C1", NX + NU, 2 * NX, REAL, FORCES_PARAMS, false, qpObjPrefix);
//	//	else
//	//		conC[ 0 ].setup("C1", NX + NU, NX, REAL, FORCES_PARAMS, false, qpObjPrefix);
//
//	//	for (unsigned i = 1; i < N; ++i)
//	for (unsigned i = 0; i < N; ++i)
//		conC[ i ].setup(string("C") + toString(i + 1), NX + NU, NX, REAL, FORCES_PARAMS, false, qpObjPrefix);
//
//	ExportIndex index( "index" );
//	conStageC.setup("conStageC", NX + NU, NX, REAL);
//	conSetGxGu.setup("conSetGxGu", conStageC, index);
//
//	conSetGxGu.addStatement(
//			conStageC.getSubMatrix(0, NX, 0, NX) ==
//					evGx.getSubMatrix(index * NX, (index + 1) * NX, 0, NX).getTranspose()
//	);
//	conSetGxGu.addLinebreak();
//	conSetGxGu.addStatement(
//			conStageC.getSubMatrix(NX, NX + NU, 0, NX) ==
//					evGu.getSubMatrix(index * NX, (index + 1) * NX, 0, NU).getTranspose()
//	);
//
//	//	if (initialStateFixed() == true)
//	//	{
//	//		initialize.addStatement(
//	//				conC[ 0 ].getSubMatrix(0, NX, 0, NX) == eye( NX )
//	//		);
//	//		evaluateConstraints.addLinebreak();
//	//		evaluateConstraints.addStatement(
//	//				conC[ 0 ].getSubMatrix(0, NX, NX, 2 * NX) == evGx.getSubMatrix(0, NX, 0, NX).getTranspose()
//	//		);
//	//		evaluateConstraints.addLinebreak();
//	//		evaluateConstraints.addStatement(
//	//				conC[ 0 ].getSubMatrix(NX, NX + NU, NX, 2 * NX) == evGu.getSubMatrix(0, NX, 0, NU).getTranspose()
//	//		);
//	//		evaluateConstraints.addLinebreak();
//	//	}
//
//	unsigned start = 0; //initialStateFixed() == true ? 1 : 0;
//	for (unsigned i = start; i < N; ++i)
//		evaluateConstraints.addFunctionCall(conSetGxGu, conC[ i ], ExportIndex( i ));
//	evaluateConstraints.addLinebreak();
//
//	cond.clear();
//
//	unsigned dNum = initialStateFixed() == true ? N + 1 : N;
//	cond.resize(dNum);
//
//	//	if (initialStateFixed() == true)
//	//		cond[ 0 ].setup("d1", 2 * NX, 1, REAL, FORCES_PARAMS, false, qpObjPrefix);
//	//	else
//	//		cond[ 0 ].setup("d1", NX, 1, REAL, FORCES_PARAMS, false, qpObjPrefix);
//
//	for (unsigned i = 0; i < dNum; ++i)
//		cond[ i ].setup(string("d") + toString(i + 1), NX, 1, REAL, FORCES_PARAMS, false, qpObjPrefix);
//
//	ExportVariable staged, stagedNew;
//
//	staged.setup("staged", NX, 1, REAL, ACADO_LOCAL);
//	stagedNew.setup("stagedNew", NX, 1, REAL, ACADO_LOCAL);
//	conSetd.setup("conSetd", stagedNew, index);
//	conSetd.addStatement(
//			stagedNew == zeros<double>(NX, 1) - d.getRows(index * NX, (index + 1) * NX)
//	);
//
//	//		evaluateConstraints.addStatement(
//	//				cond[ 0 ].getRows(NX, 2 * NX) == dummyZero - d.getRows(0, NX)
//	//		);
//	//		evaluateConstraints.addLinebreak();
//
//	if( initialStateFixed() ) {
//		for (unsigned i = 1; i < dNum; ++i)
//			evaluateConstraints.addFunctionCall(
//					conSetd, cond[ i ], ExportIndex(i - 1)
//			);
//	}
//	else {
//		for (unsigned i = 0; i < dNum; ++i)
//			evaluateConstraints.addFunctionCall(
//					conSetd, cond[ i ], ExportIndex(i)
//			);
//	}

	return SUCCESSFUL_RETURN;
}

returnValue ExportGaussNewtonBlockForces::setupEvaluation( )
{
	////////////////////////////////////////////////////////////////////////////
	//
	// Preparation phase
	//
	////////////////////////////////////////////////////////////////////////////

	preparation.setup( "preparationStep" );
	preparation.doc( "Preparation step of the RTI scheme." );
	ExportVariable retSim("ret", 1, 1, INT, ACADO_LOCAL, true);
	retSim.setDoc("Status of the integration module. =0: OK, otherwise the error code.");
	preparation.setReturnValue(retSim, false);
	ExportIndex index("index");
	preparation.addIndex( index );

	preparation	<< retSim.getFullName() << " = " << modelSimulation.getName() << "();\n";

	preparation.addFunctionCall( evaluateObjective );
	if( regularizeHessian.isDefined() ) preparation.addFunctionCall( regularizeHessian );
	preparation.addFunctionCall( evaluateConstraints );

	preparation.addLinebreak();
	preparation << (Dy -= y) << (DyN -= yN);
	preparation.addLinebreak();

	ExportForLoop condensePrepLoop( index, 0, getNumberOfBlocks() );
	condensePrepLoop.addFunctionCall( condensePrep, index );
	preparation.addStatement( condensePrepLoop );

	preparation.addStatement( objHessians[N] == QN1 );
	DMatrix mReg = eye<double>( getNX() );
	mReg *= levenbergMarquardt;
	preparation.addStatement( objHessians[N] += mReg );
	preparation.addLinebreak();

	preparation.addStatement( objGradients[ N ] == QN2 * DyN );
	int variableObjS;
	get(CG_USE_VARIABLE_WEIGHTING_MATRIX, variableObjS);
	ExportVariable SlxCall =
				objSlx.isGiven() == true || variableObjS == false ? objSlx : objSlx.getRows(N * NX, (N + 1) * NX);
	preparation.addStatement( objGradients[ N ] += SlxCall );
	preparation.addLinebreak();

	////////////////////////////////////////////////////////////////////////////
	//
	// Feedback phase
	//
	////////////////////////////////////////////////////////////////////////////

	ExportVariable tmp("tmp", 1, 1, INT, ACADO_LOCAL, true);
	tmp.setDoc( "Status code of the qpOASES QP solver." );

	feedback.setup("feedbackStep");
	feedback.doc( "Feedback/estimation step of the RTI scheme." );
	feedback.setReturnValue( tmp );
	feedback.addIndex( index );

	if (initialStateFixed() == true)
	{
		feedback.addStatement( cond[ 0 ] == x0 - x.getRow( 0 ).getTranspose() );
	}
	feedback.addLinebreak();

	//
	// Configure output variables
	//
	std::vector< ExportVariable > vecQPVars;

	vecQPVars.clear();
	vecQPVars.resize(N + 1);
	for (unsigned i = 0; i < N; ++i)
		vecQPVars[ i ].setup(string("out") + toString(i + 1), NX + NU, 1, REAL, FORCES_OUTPUT, false, qpObjPrefix);
	vecQPVars[ N ].setup(string("out") + toString(N + 1), NX, 1, REAL, FORCES_OUTPUT, false, qpObjPrefix);

	//
	// In case warm starting is enabled, give an initial guess, based on the old solution
	//
	int hotstartQP;
	get(HOTSTART_QP, hotstartQP);

	if ( hotstartQP )
	{
		std::vector< ExportVariable > zInit;

		zInit.clear();
		zInit.resize(N + 1);
		for (unsigned i = 0; i < N; ++i)
		{
			string name = "z_init_";
			name = name + (i < 10 ? "0" : "") + toString( i );
			zInit[ i ].setup(name, NX + NU, 1, REAL, FORCES_PARAMS, false, qpObjPrefix);
		}
		string name = "z_init_";
		name = name + (N < 10 ? "0" : "") + toString( N );
		zInit[ N ].setup(name, NX, 1, REAL, FORCES_PARAMS, false, qpObjPrefix);

		// TODO This should be further investigated.

		//
		// 1) Just use the old solution
		//
		//		for (unsigned blk = 0; blk < N + 1; blk++)
		//			feedback.addStatement(zInit[ blk ] == vecQPVars[ blk ] );

		//
		// 2) Initialization by shifting
		//

		//		for (unsigned blk = 0; blk < N - 1; blk++)
		//			feedback.addStatement( zInit[ blk ] == vecQPVars[blk + 1] );
		//		for (unsigned el = 0; el < NX; el++)
		//			feedback.addStatement( zInit[N - 1].getElement(el, 0) == vecQPVars[ N ].getElement(el, 0) );
	}

	//
	// Call a QP solver
	// NOTE: we need two prefixes:
	// 1. module prefix
	// 2. structure instance prefix
	//
	ExportFunction solveQP;
	solveQP.setup("solve");

	feedback
	<< tmp.getFullName() << " = "
	<< qpModuleName << "_" << solveQP.getName() << "( "
	<< "&" << qpObjPrefix << "_" << "params" << ", "
	<< "&" << qpObjPrefix << "_" << "output" << ", "
	<< "&" << qpObjPrefix << "_" << "info" << " );\n";
	feedback.addLinebreak();

	ExportForLoop expandLoop( index, 0, getNumberOfBlocks() );
	expandLoop.addFunctionCall( expand, index );
	feedback.addStatement( expandLoop );

	feedback.addStatement( x.getRow( N ) += vecQPVars[ N ].getTranspose() );
	feedback.addLinebreak();

	////////////////////////////////////////////////////////////////////////////
	//
	// Setup evaluation of the KKT tolerance
	//
	////////////////////////////////////////////////////////////////////////////

	ExportVariable kkt("kkt", 1, 1, REAL, ACADO_LOCAL, true);

	getKKT.setup( "getKKT" );
	getKKT.doc( "Get the KKT tolerance of the current iterate. Under development." );
	//	kkt.setDoc( "The KKT tolerance value." );
	kkt.setDoc( "1e-15." );
	getKKT.setReturnValue( kkt );

	getKKT.addStatement( kkt == 1e-15 );

	return SUCCESSFUL_RETURN;
}

returnValue ExportGaussNewtonBlockForces::setupQPInterface( )
{
	//
	// Configure and export QP interface
	//

	qpInterface = std::tr1::shared_ptr< ExportForcesInterface >(new ExportForcesInterface(FORCES_TEMPLATE, "", commonHeaderName));

	ExportVariable tmp1("tmp", 1, 1, REAL, FORCES_PARAMS, false, qpObjPrefix);
	ExportVariable tmp2("tmp", 1, 1, REAL, FORCES_OUTPUT, false, qpObjPrefix);
	ExportVariable tmp3("tmp", 1, 1, REAL, FORCES_INFO, false, qpObjPrefix);

	string params = qpModuleName + "_params";

	string output = qpModuleName + "_output";

	string info = qpModuleName + "_info";

	string header = qpModuleName + ".h";

	qpInterface->configure(
			header,

			params,
			tmp1.getDataStructString(),

			output,
			tmp2.getDataStructString(),

			info,
			tmp3.getDataStructString()
	);

	//
	// Configure and export MATLAB QP generator
	//

	string folderName;
	get(CG_EXPORT_FOLDER_NAME, folderName);
	string outFile = folderName + "/acado_forces_generator.m";

	qpGenerator = std::tr1::shared_ptr< ExportForcesGenerator >(new ExportForcesGenerator(FORCES_GENERATOR, outFile, "", "real_t", "int", 16, "%"));

	int maxNumQPiterations;
	get( MAX_NUM_QP_ITERATIONS,maxNumQPiterations );

	int printLevel;
	get(PRINTLEVEL, printLevel);

	// if not specified, use default value
	if ( maxNumQPiterations <= 0 )
		maxNumQPiterations = 3 * getNumQPvars();

	int useOMP;
	get(CG_USE_OPENMP, useOMP);

	int hotstartQP;
	get(HOTSTART_QP, hotstartQP);

	qpGenerator->configure(
			NX,
			NU,
			N,
			conLBIndices,
			conUBIndices,
			conABIndices,
			(Q1.isGiven() == true && R1.isGiven() == true) ? 1 : 0,
					diagonalH,
					diagonalHN,
					initialStateFixed(),
					qpModuleName,
					(PrintLevel)printLevel == HIGH ? 2 : 0,
							maxNumQPiterations,
							useOMP,
							true,
							hotstartQP
	);

	qpGenerator->exportCode();

	//
	// Export Python generator
	//
	ACADOWARNINGTEXT(RET_NOT_IMPLEMENTED_YET,
			"A python code generator interface for block condensing with FORCES is under development.");

	return SUCCESSFUL_RETURN;
}


CLOSE_NAMESPACE_ACADO
