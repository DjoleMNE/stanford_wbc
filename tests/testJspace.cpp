/*
 * Stanford Whole-Body Control Framework http://stanford-wbc.sourceforge.net/
 *
 * Copyright (c) 2010 Stanford University. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>
 */

/**
   \file testJspace.cpp
   \author Roland Philippsen
*/

#define PR_DOUBLE_PRECISION
#include "puma/pumaDynamics.h"

#include <wbc/core/RobotControlModel.hpp>
#include <wbc/core/BranchingRepresentation.hpp>
#include <wbc/parse/BRParser.hpp>
#include <tao/dynamics/taoDNode.h>
#include <jspace/Model.hpp>
#include <wbcnet/strutil.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <gtest/gtest.h>
#include <errno.h>
#include <string.h>

using namespace std;


static std::string create_tmp(char const * fname_template, char const * contents) throw(runtime_error);
static std::string create_tmp_xml() throw(runtime_error);
static std::string create_tmp_frames() throw(runtime_error);
static wbc::BranchingRepresentation * create_brep() throw(runtime_error);
static jspace::Model * create_model() throw(runtime_error);

static string xml_filename("");
static string frames_filename("");


TEST (jspaceModel, state)
{
  jspace::Model * model(0);
  try {
    model = create_model();
    int const ndof(model->getNDOF());
    jspace::State state_in(ndof, ndof);
    if (0 != gettimeofday(&state_in.acquisition_time_, 0)) {
      FAIL () << "gettimeofday(): " << strerror(errno);
    }
    for (int ii(0); ii < ndof; ++ii) {
      state_in.joint_angles_[ii] = -3 + ii * 0.5;
      state_in.joint_velocities_[ii] = 3 - ii * 0.25;
    }
    model->setState(state_in);
    {
      jspace::State state_out(ndof, ndof);
      state_out = model->getState();
      EXPECT_TRUE (state_out.equal(state_in, jspace::State::COMPARE_ALL, 1e-6))
	<< "jspace::State assignment or jspace::Model::getState() is buggy";
    }
    {
      jspace::State state_out(model->getState());
      EXPECT_TRUE (state_out.equal(state_in, jspace::State::COMPARE_ALL, 1e-6))
	<< "jspace::State copy ctor or jspace::Model::getState() is buggy";
    }
  }
  catch (std::exception const & ee) {
    ADD_FAILURE () << "exception " << ee.what();
  }
  delete model;
}


TEST (jspaceModel, branching)
{
  jspace::Model * model(0);
  try {
    model = create_model();
    EXPECT_EQ (model->getNNodes(), 7) << "Puma should have 7 nodes";
    EXPECT_EQ (model->getNJoints(), 6) << "Puma should have 6 joints";
    EXPECT_EQ (model->getNDOF(), 6) << "Puma should have 6 DOF";
    char const * node_name[] = {
      "base",
      "upper_arm",
      "lower_arm",
      "wrist-hand",
      "wrist-finger",
      "end-effector"
    };
    char const * joint_name[] = {
      "shoulder-yaw",
      "shoulder-pitch",
      "elbow",
      "wrist-roll1",
      "wrist-pitch",
      "wrist-roll2"
    };
    for (int ii(0); ii < 6; ++ii) {
      EXPECT_NE ((void*)0, model->getNode(ii))
	<< "Could not get node " << ii;
      EXPECT_NE ((void*)0, model->getNodeByName(node_name[ii]))
	<< "Could not get node by name \"" << node_name[ii] << "\"";
      EXPECT_EQ (ii, model->getNodeByName(node_name[ii])->getID())
	<< "Node with name \"" << node_name[ii] << "\" should have ID " << ii;
      EXPECT_NE ((void*)0, model->getNodeByJointName(joint_name[ii]))
	<< "Could not get node by joint name \"" << joint_name[ii] << "\"";
      EXPECT_EQ (ii, model->getNodeByJointName(joint_name[ii])->getID())
	<< "Node with joint name \"" << joint_name[ii] << "\" should have ID " << ii;
    }
  }
  catch (std::exception const & ee) {
    ADD_FAILURE () << "exception " << ee.what();
  }
  delete model;
}


TEST (jspaceModel, kinematics)
{
  jspace::Model * model(0);
  try {
    model = create_model();
    int const ndof(model->getNDOF());
    jspace::State state(ndof, ndof);
    
    if (frames_filename.empty()) {
      frames_filename = create_tmp_frames();
    }
    ifstream is(frames_filename.c_str());

    string line;
    int joint_positions_count(0);
    int link_origins_count(0);
    int line_count(0);

    while (getline(is, line)) {
      ++line_count;

      if (line.empty() || ('=' == line[0])) {
	continue;
      }
      vector<string> token;
      if (1 > sfl::tokenize(line, ':', token)) {
	continue;
      }
      
      if ("joint_positions" == token[0]) {
	if (2 > token.size()) {
	  FAIL () << frames_filename << ": line " << line_count << ": no joint positions, expected " << ndof;
	}
	istringstream ipos(token[1]);
	for (int ii(0); ii < ndof; ++ii) {
	  ipos >> state.joint_angles_[ii];
	}
	if ( ! ipos) {
	  FAIL () << frames_filename << ": line " << line_count << ": not enough joint positions, expected " << ndof;
	}
	model->update(state);
	++joint_positions_count;
	continue;
      }
      
      if ("link_origins" == token[0]) {
	++link_origins_count;
	continue;
      }
      
      if (4 > token.size()) {
	FAIL () << frames_filename << ": line " << line_count << ": expected ID-frame entry";
      }
      
      if (joint_positions_count != link_origins_count) {
	FAIL () << frames_filename << ": line " << line_count << ": joint_positions_count != link_origins_count";
      }
      
      int id;
      float rx, ry, rz, rw, tx, ty, tz;
      if (8 != sscanf(line.c_str(),
		      " ID %d: r: { %f %f %f %f } t: { %f %f %f }",
		      &id, &rx, &ry, &rz, &rw, &tx, &ty, &tz)) {
	FAIL () << frames_filename << ": line " << line_count << ": could not parse ID-frame entry";
      }
      if (ndof <= id) {
	FAIL () << frames_filename << ": line " << line_count << ": ID-frame entry " << id << " exceeds NDOF " << ndof;
      }
      
      SAITransform transform;
      if ( ! model->getGlobalFrame(model->getNode(id), transform)) {
	FAIL() << frames_filename << ": line " << line_count << ": could not get global frame " << id << " from model";
      }
      EXPECT_TRUE (transform.rotation().equal(SAIQuaternion(rw, rx, ry, rz), 1e-6))
	<< "rotation mismatch\n"
	<< "  entry: " << joint_positions_count << "\n"
	<< "  pos: " << state.joint_angles_ << "\n"
	<< "  ID: " << id << "\n"
	<< "  expected: " << SAIQuaternion(rw, rx, ry, rz) << "\n"
	<< "  computed: " << transform.rotation();
      EXPECT_TRUE (transform.translation().equal(SAIVector3(tx, ty, tz), 1e-6))
	<< "translation mismatch\n"
	<< "  entry: " << joint_positions_count << "\n"
	<< "  pos: " << state.joint_angles_ << "\n"
	<< "  ID: " << id << "\n"
	<< "  expected: " << SAIVector3(tx, ty, tz) << "\n"
	<< "  computed: " << transform.translation();
      
#ifdef VERBOSE
      cout << "PASSED transform check\n"
	   << "  entry: " << joint_positions_count << "\n"
	   << "  pos: " << state.joint_angles_ << "\n"
	   << "  ID: " << id << "\n"
	   << "  rotation:\n"
	   << "    expected: " << SAIQuaternion(rw, rx, ry, rz) << "\n"
	   << "    computed: " << transform.rotation() << "\n"
	   << "  translation:\n"
	   << "    expected: " << SAIVector3(tx, ty, tz) << "\n"
	   << "    computed: " << transform.translation() << "\n";
#endif // VERBOSE
    }
  }
  catch (std::exception const & ee) {
    ADD_FAILURE () << "exception " << ee.what();
  }
  delete model;
}


static bool check_matrix(char const * name,
			 PrMatrix const & want,
			 SAIMatrix const & have,
			 double precision,
			 std::ostringstream & msg)
{
  int const nrows(want.row());
  if (nrows != have.row()) {
    msg << "check_matrix(" << name << ") size mismatch: have " << have.row()
	<< " rows but want " << nrows << "\n";
    return false;
  }
  int const ncolumns(want.column());
  if (ncolumns != have.column()) {
    msg << "check_matrix(" << name << ") size mismatch: have " << have.column()
	<< " columns but want " << ncolumns << "\n";
    return false;
  }
  
  precision = fabs(precision);
  double maxdelta(0);
  for (int ii(0); ii < nrows; ++ii) {
    for (int jj(0); jj < ncolumns; ++jj) {
      double const delta(fabs(have[ii][jj] - want[ii][jj]));
      if (delta > precision) {
	maxdelta = delta;
      }
    }
  }
  double const halfmax(0.5 * maxdelta);
  double const tenprecision(10 * precision);
  
  if (maxdelta <= precision) {
    msg << "check_matrix(" << name << ") OK\n";
  }
  else {
    std::string const wtf(name);
    msg << "check_matrix(";
    msg << wtf;
    msg << ") FAILED\n";
  }
  msg << "  precision = " << precision << "\n"
      << "  maxdelta = " << maxdelta << "\n";
  for (int ii(nrows-1); ii >= 0; --ii) {
    msg << "    ";
    for (int jj(0); jj < ncolumns; ++jj) {
      double const delta(fabs(have[ii][jj] - want[ii][jj]));
      if (delta <= precision) {
	if (delta < halfmax) {
	  msg << ".";
	}
	else {
	  msg << "o";
	}
      }
      else if (delta >= tenprecision) {
	msg << "#";
      }
      else {
	msg << "*";
      }
    }
    msg << "\n";
  }
  
  return maxdelta <= precision;
}


static bool check_vector(char const * name,
			 PrVector const & want,
			 SAIVector const & have,
			 double precision,
			 std::ostream & msg)
{
  int const nelems(want.size());
  if (nelems != have.size()) {
    msg << "check_matrix(" << name << ") size mismatch: have " << have.size()
	<< " elements but want " << nelems << "\n";
    return false;
  }
  
  precision = fabs(precision);
  double maxdelta(0);
  for (int ii(0); ii < nelems; ++ii) {
    double const delta(fabs(have[ii] - want[ii]));
    if (delta > precision) {
      maxdelta = delta;
    }
  }
  double const halfmax(0.5 * maxdelta);
  double const tenprecision(10 * precision);
  
  if (maxdelta <= precision) {
    msg << "check_vector(" << name << ") OK\n";
  }
  else {
    msg << "check_vector(" << name << ") FAILED\n";
  }
  msg << "  precision = " << precision << "\n"
      << "  maxdelta = " << maxdelta << "\n"
      << "    ";
  for (int ii(0); ii < nelems; ++ii) {
    double const delta(fabs(have[ii] - want[ii]));
    if (delta <= precision) {
      if (delta < halfmax) {
	msg << ".";
      }
      else {
	msg << "o";
      }
    }
    else if (delta >= tenprecision) {
      msg << "#";
    }
    else {
      msg << "*";
    }
  }
  msg << "\n";
  
  return maxdelta <= precision;
}


TEST (jspaceModel, dynamics)
{
  jspace::Model * model(0);
  try {
    model = create_model();
    taoDNode * ee(model->getNode(5));
    ASSERT_NE ((void*)0, ee) << "no end effector (node ID 5)";
    jspace::State state(6, 6);
    SAIVector tB(6), tG(6);
    SAIMatrix tJ(6, 6), tA(6, 6);
    PrVector mq(6), mdq(6), mB(6), mG(6);
    PrMatrix mJ(6, 6), mdJ(6, 6), mA(6, 6);
    state.joint_velocities_.zero();
    mdq.zero();
    for (mq[0] = -0.1; mq[0] < 0.11; mq[0] += 0.1) {
      state.joint_angles_[0] = mq[0];
      for (mq[1] = -0.1; mq[1] < 0.11; mq[1] += 0.1) {
	state.joint_angles_[1] = mq[1];
	for (mq[2] = -0.1; mq[2] < 0.11; mq[2] += 0.1) {
	  state.joint_angles_[2] = mq[2];
	  for (mq[3] = -0.1; mq[3] < 0.11; mq[3] += 0.1) {
	    state.joint_angles_[3] = mq[3];
	    for (mq[4] = -0.1; mq[4] < 0.11; mq[4] += 0.1) {
	      state.joint_angles_[4] = mq[4];
	      for (mq[5] = -0.1; mq[5] < 0.11; mq[5] += 0.1) {
		state.joint_angles_[5] = mq[5];
		getPumaDynamics(mq, mdq, mJ, mdJ, mA, mB, mG);
		model->update(state);
		ASSERT_TRUE (model->computeJacobian(ee, tJ)) << "computeJacobian failed";
		model->getMassInertia(tA);
		model->getCoriolisCentrifugal(tB);
		model->getGravity(tG);
		std::ostringstream msg;

		msg << "wtf???\n";
		
		EXPECT_TRUE (check_matrix("Jacobian", mJ, tJ, 1e-3, msg)) << msg.str();
		EXPECT_TRUE (check_matrix("mass inertia", mA, tA, 1e-3, msg)) << msg.str();
		EXPECT_TRUE (check_vector("Coriolis centrifugal", mB, tB, 1e-3, msg)) << msg.str();
		EXPECT_TRUE (check_vector("gravity", mG, tG, 1e-3, msg)) << msg.str();
	      }
	    }
	  }
	}
      }
    }
  }
  catch (std::exception const & ee) {
    ADD_FAILURE () << "exception " << ee.what();
  }
  delete model;
}


int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS ();
}


std::string create_tmp(char const * fname_template, char const * contents) throw(runtime_error)
{
  if (strlen(fname_template) >= 64) {
    throw runtime_error("create_tmp(): fname_template is too long (max 63 characters)");
  }
  
  static char tmpname[64];
  memset(tmpname, '\0', 64);
  strncpy(tmpname, fname_template, 63);
  int const tmpfd(mkstemp(tmpname));
  if (-1 == tmpfd) {
    throw runtime_error("create_tmp(): mkstemp(): " + string(strerror(errno)));
  }
  
  size_t const len(strlen(contents));
  if (len != write(tmpfd, contents, len)) {
    throw runtime_error("create_tmp(): write(): " + string(strerror(errno)));
  }
  close(tmpfd);
  
  string result(tmpname);
  return result;
}


std::string create_tmp_xml() throw(runtime_error)
{
  static char const * xml = 
    "<?xml version=\"1.0\" ?>\n"
    "<dynworld>\n"
    "  <baseNode>\n"
    "    <robotName>Puma</robotName>\n"
    "    <gravity>0, 0, -9.81</gravity>\n"
    "    <pos>0, 0, 0</pos>\n"
    "    <rot>1, 0, 0, 0</rot>\n"
    "    <name>base</name>\n"
    "    <ID>-1</ID>\n"
    "    <jointNode>\n"
    "      <jointName>shoulder-yaw</jointName>\n"
    "      <linkName>base</linkName>\n"
    "      <upperJointLimit>0.1</upperJointLimit>\n"
    "      <lowerJointLimit>-0.1</lowerJointLimit>\n"
    "      <defaultJointPosition>0</defaultJointPosition>\n"
    "      <type>R</type>\n"
    "      <axis>Z</axis>\n"
    "      <mass>34.40</mass>\n"
    "      <inertia>0.00, 0.00, 1.49</inertia>\n"
    "      <com>0, 0, 0</com>\n"
    "      <pos>0, 0, 0</pos>\n"
    "      <rot>0, 0, 1, 0</rot>\n"
    "      <name>NoName</name>\n"
    "      <ID>0</ID>\n"
    "      <jointNode>\n"
    "        <jointName>shoulder-pitch</jointName>\n"
    "        <linkName>upper_arm</linkName>\n"
    "        <upperJointLimit>0.1</upperJointLimit>\n"
    "        <lowerJointLimit>-0.1</lowerJointLimit>\n"
    "        <defaultJointPosition>0</defaultJointPosition>\n"
    "        <type>R</type>\n"
    "        <axis>Z</axis>\n"
    "        <mass>17.40</mass>\n"
    "        <inertia>0.13, 0.524, 5.249</inertia>\n"
    "        <com>0.068, 0.006, -0.016</com>\n"
    "        <pos>0.0, 0.2435, 0.0</pos>\n"
    "        <rot>1, 0, 0, -1.57079632679489661923</rot>\n"
    "        <name>upper_arm</name>\n"
    "        <ID>1</ID>\n"
    "        <jointNode>\n"
    "          <jointName>elbow</jointName>\n"
    "          <linkName>lower_arm</linkName>\n"
    "          <upperJointLimit>0.1</upperJointLimit>\n"
    "          <lowerJointLimit>-0.1</lowerJointLimit>\n"
    "          <defaultJointPosition>0</defaultJointPosition>\n"
    "          <type>R</type>\n"
    "          <axis>Z</axis>\n"
    "          <mass>6.04</mass>\n"
    "          <inertia>0.192, 0.0154, 1.042</inertia>\n"
    "          <com>0, -0.143, 0.014</com>\n"
    "          <pos>0.4318, 0, -0.0934</pos>\n"
    "          <rot>1, 0, 0, 0</rot>\n"
    "          <name>lower_arm</name>\n"
    "          <ID>2</ID>\n"
    "          <jointNode>\n"
    "            <jointName>wrist-roll1</jointName>\n"
    "            <linkName>wrist-hand</linkName>\n"
    "            <upperJointLimit>0.1</upperJointLimit>\n"
    "            <lowerJointLimit>-0.1</lowerJointLimit>\n"
    "            <defaultJointPosition>0</defaultJointPosition>\n"
    "            <type>R</type>\n"
    "            <axis>Z</axis>\n"
    "            <mass>0.82</mass>\n"
    "            <inertia>0.0018, 0.0018, 0.2013</inertia>\n"
    "            <com>0.0, 0.0, -0.019</com>\n"
    "            <pos>-0.0203, -0.4331, 0.0</pos>\n"
    "            <rot>1, 0, 0, 1.57079632679489661923</rot>\n"
    "            <name>wrist-hand</name>\n"
    "            <ID>3</ID>\n"
    "            <jointNode>\n"
    "              <jointName>wrist-pitch</jointName>\n"
    "              <linkName>wrist-finger</linkName>\n"
    "              <upperJointLimit>0.1</upperJointLimit>\n"
    "              <lowerJointLimit>-0.1</lowerJointLimit>\n"
    "              <defaultJointPosition>0</defaultJointPosition>\n"
    "              <type>R</type>\n"
    "              <axis>Z</axis>\n"
    "              <mass>0.34</mass>\n"
    "              <inertia>0.0003, 0.0003, 0.1794</inertia>\n"
    "              <com>0.0, 0.0, 0.0</com>\n"
    "              <pos>0, 0, 0</pos>\n"
    "              <rot>1, 0, 0, -1.57079632679489661923</rot>\n"
    "              <name>wrist-finger</name>\n"
    "              <ID>4</ID>\n"
    "              <jointNode>\n"
    "                <jointName>wrist-roll2</jointName>\n"
    "                <linkName>end-effector</linkName>\n"
    "                <upperJointLimit>0.1</upperJointLimit>\n"
    "                <lowerJointLimit>-0.1</lowerJointLimit>\n"
    "                <defaultJointPosition>0</defaultJointPosition>\n"
    "                <type>R</type>\n"
    "                <axis>Z</axis>\n"
    "                <mass>0.09</mass>\n"
    "                <inertia>0.00015, 0.00015, 0.19304</inertia>\n"
    "                <com>0.0, 0.0, 0.032</com>\n"
    "                <pos>0, 0, 0</pos>\n"
    "                <rot>1, 0, 0, 1.57079632679489661923</rot>\n"
    "                <name>end-effector</name>\n"
    "                <ID>5</ID>\n"
    "              </jointNode>\n"
    "            </jointNode>\n"
    "          </jointNode>\n"
    "        </jointNode>\n"
    "      </jointNode>\n"
    "    </jointNode>\n"
    "  </baseNode>\n"
    "</dynworld>\n";
  std::string result(create_tmp("puma.xml.XXXXXX", xml));
  return result;
}


std::string create_tmp_frames() throw(runtime_error)
{
  static char const * frames = 
    "==================================================\n"
    "joint_positions:	0	0	0	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  0  1 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.707107  0  0  0.707107 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  0  0  1 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0.7854	0	0	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0.382684  0.923879 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.653281  -0.270599  0.270599  0.653281 }  t: { -0.172181  0.17218  0 }\n"
    "  ID 2: r: { -0.653281  -0.270599  0.270599  0.653281 }  t: { 0.199191  0.411466  -2.77556e-17 }\n"
    "  ID 3: r: { 0  0  0.382684  0.923879 }  t: { 0.184837  0.397112  0.4331 }\n"
    "  ID 4: r: { -0.653281  -0.270599  0.270599  0.653281 }  t: { 0.184837  0.397112  0.4331 }\n"
    "  ID 5: r: { 0  0  0.382684  0.923879 }  t: { 0.184837  0.397112  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	0.7854	0	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.653281  0.270599  0.270599  0.653281 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.653281  0.270599  0.270599  0.653281 }  t: { 0.305328  0.1501  -0.305329 }\n"
    "  ID 3: r: { 0  0.382684  8.32667e-17  0.923879 }  t: { 0.597222  0.1501  0.0152724 }\n"
    "  ID 4: r: { -0.653281  0.270599  0.270599  0.653281 }  t: { 0.597222  0.1501  0.0152724 }\n"
    "  ID 5: r: { 0  0.382684  8.32667e-17  0.923879 }  t: { 0.597222  0.1501  0.0152724 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0.7854	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.653281  0.270599  0.270599  0.653281 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0.382684  8.32667e-17  0.923879 }  t: { 0.723694  0.1501  0.320602 }\n"
    "  ID 4: r: { -0.653281  0.270599  0.270599  0.653281 }  t: { 0.723694  0.1501  0.320602 }\n"
    "  ID 5: r: { 0  0.382684  8.32667e-17  0.923879 }  t: { 0.723694  0.1501  0.320602 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	0.7854	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  0.382684  0.923879 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.653281  -0.270599  0.270599  0.653281 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  0  0.382684  0.923879 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	0	0.7854	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  0  1 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.653281  0.270599  0.270599  0.653281 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  0.382684  8.32667e-17  0.923879 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	0	0	0.7854	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  0  1 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.707107  0  0  0.707107 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  0  0.382684  0.923879 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	-0.7854	0	0	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  -0.382684  0.923879 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.653281  0.270599  -0.270599  0.653281 }  t: { 0.172181  0.17218  0 }\n"
    "  ID 2: r: { -0.653281  0.270599  -0.270599  0.653281 }  t: { 0.411465  -0.199193  -2.77556e-17 }\n"
    "  ID 3: r: { 0  0  -0.382684  0.923879 }  t: { 0.397111  -0.184838  0.4331 }\n"
    "  ID 4: r: { -0.653281  0.270599  -0.270599  0.653281 }  t: { 0.397111  -0.184838  0.4331 }\n"
    "  ID 5: r: { 0  0  -0.382684  0.923879 }  t: { 0.397111  -0.184838  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	-0.7854	0	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.653281  -0.270599  -0.270599  0.653281 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.653281  -0.270599  -0.270599  0.653281 }  t: { 0.305328  0.1501  0.305329 }\n"
    "  ID 3: r: { 0  -0.382684  -8.32667e-17  0.923879 }  t: { -0.0152746  0.1501  0.597222 }\n"
    "  ID 4: r: { -0.653281  -0.270599  -0.270599  0.653281 }  t: { -0.0152746  0.1501  0.597222 }\n"
    "  ID 5: r: { 0  -0.382684  -8.32667e-17  0.923879 }  t: { -0.0152746  0.1501  0.597222 }\n"
    "==================================================\n"
    "joint_positions:	0	0	-0.7854	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.653281  -0.270599  -0.270599  0.653281 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  -0.382684  -8.32667e-17  0.923879 }  t: { 0.111197  0.1501  0.291893 }\n"
    "  ID 4: r: { -0.653281  -0.270599  -0.270599  0.653281 }  t: { 0.111197  0.1501  0.291893 }\n"
    "  ID 5: r: { 0  -0.382684  -8.32667e-17  0.923879 }  t: { 0.111197  0.1501  0.291893 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	-0.7854	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  -0.382684  0.923879 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.653281  0.270599  -0.270599  0.653281 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  0  -0.382684  0.923879 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	0	-0.7854	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  0  1 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.653281  -0.270599  -0.270599  0.653281 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  -0.382684  -8.32667e-17  0.923879 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	0	0	-0.7854	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  0  1 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.707107  0  0  0.707107 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  0  -0.382684  0.923879 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	1.5708	0	0	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0.707108  0.707105 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.499999  -0.500001  0.500001  0.499999 }  t: { -0.2435  -8.94425e-07  0 }\n"
    "  ID 2: r: { -0.499999  -0.500001  0.500001  0.499999 }  t: { -0.150102  0.431799  -5.55112e-17 }\n"
    "  ID 3: r: { 0  0  0.707108  0.707105 }  t: { -0.150102  0.411499  0.4331 }\n"
    "  ID 4: r: { -0.499999  -0.500001  0.500001  0.499999 }  t: { -0.150102  0.411499  0.4331 }\n"
    "  ID 5: r: { 0  0  0.707108  0.707105 }  t: { -0.150102  0.411499  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	1.5708	0	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.499999  0.500001  0.500001  0.499999 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.499999  0.500001  0.500001  0.499999 }  t: { -1.58609e-06  0.1501  -0.4318 }\n"
    "  ID 3: r: { 0  0.707108  1.11022e-16  0.707105 }  t: { 0.433098  0.1501  -0.411502 }\n"
    "  ID 4: r: { -0.499999  0.500001  0.500001  0.499999 }  t: { 0.433098  0.1501  -0.411502 }\n"
    "  ID 5: r: { 0  0.707108  1.11022e-16  0.707105 }  t: { 0.433098  0.1501  -0.411502 }\n"
    "==================================================\n"
    "joint_positions:	0	0	1.5708	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.499999  0.500001  0.500001  0.499999 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0.707108  1.11022e-16  0.707105 }  t: { 0.8649  0.1501  0.0202984 }\n"
    "  ID 4: r: { -0.499999  0.500001  0.500001  0.499999 }  t: { 0.8649  0.1501  0.0202984 }\n"
    "  ID 5: r: { 0  0.707108  1.11022e-16  0.707105 }  t: { 0.8649  0.1501  0.0202984 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	1.5708	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  0.707108  0.707105 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.499999  -0.500001  0.500001  0.499999 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  0  0.707108  0.707105 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	0	1.5708	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  0  1 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.499999  0.500001  0.500001  0.499999 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  0.707108  1.11022e-16  0.707105 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	0	0	1.5708	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  0  1 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.707107  0  0  0.707107 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  0  0.707108  0.707105 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	-1.5708	0	0	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  -0.707108  0.707105 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.499999  0.500001  -0.500001  0.499999 }  t: { 0.2435  -8.94425e-07  0 }\n"
    "  ID 2: r: { -0.499999  0.500001  -0.500001  0.499999 }  t: { 0.150098  -0.431801  2.77556e-17 }\n"
    "  ID 3: r: { 0  0  -0.707108  0.707105 }  t: { 0.150098  -0.411501  0.4331 }\n"
    "  ID 4: r: { -0.499999  0.500001  -0.500001  0.499999 }  t: { 0.150098  -0.411501  0.4331 }\n"
    "  ID 5: r: { 0  0  -0.707108  0.707105 }  t: { 0.150098  -0.411501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	-1.5708	0	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.499999  -0.500001  -0.500001  0.499999 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.499999  -0.500001  -0.500001  0.499999 }  t: { -1.58609e-06  0.1501  0.4318 }\n"
    "  ID 3: r: { 0  -0.707108  -1.11022e-16  0.707105 }  t: { -0.433102  0.1501  0.411498 }\n"
    "  ID 4: r: { -0.499999  -0.500001  -0.500001  0.499999 }  t: { -0.433102  0.1501  0.411498 }\n"
    "  ID 5: r: { 0  -0.707108  -1.11022e-16  0.707105 }  t: { -0.433102  0.1501  0.411498 }\n"
    "==================================================\n"
    "joint_positions:	0	0	-1.5708	0	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.499999  -0.500001  -0.500001  0.499999 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  -0.707108  -1.11022e-16  0.707105 }  t: { -0.00129993  0.1501  -0.0203016 }\n"
    "  ID 4: r: { -0.499999  -0.500001  -0.500001  0.499999 }  t: { -0.00129993  0.1501  -0.0203016 }\n"
    "  ID 5: r: { 0  -0.707108  -1.11022e-16  0.707105 }  t: { -0.00129993  0.1501  -0.0203016 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	-1.5708	0	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  -0.707108  0.707105 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.499999  0.500001  -0.500001  0.499999 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  0  -0.707108  0.707105 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	0	-1.5708	0	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  0  1 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.499999  -0.500001  -0.500001  0.499999 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  -0.707108  -1.11022e-16  0.707105 }  t: { 0.4115  0.1501  0.4331 }\n"
    "==================================================\n"
    "joint_positions:	0	0	0	0	0	-1.5708	\n"
    "link_origins:\n"
    "  ID 0: r: { 0  0  0  1 }  t: { 0  0  0 }\n"
    "  ID 1: r: { -0.707107  0  0  0.707107 }  t: { 0  0.2435  0 }\n"
    "  ID 2: r: { -0.707107  0  0  0.707107 }  t: { 0.4318  0.1501  -2.0739e-17 }\n"
    "  ID 3: r: { 0  0  0  1 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 4: r: { -0.707107  0  0  0.707107 }  t: { 0.4115  0.1501  0.4331 }\n"
    "  ID 5: r: { 0  0  -0.707108  0.707105 }  t: { 0.4115  0.1501  0.4331 }\n";
  std::string result(create_tmp("puma.frames.XXXXXX", frames));
  return result;
}


wbc::BranchingRepresentation * create_brep() throw(runtime_error)
{
  if (xml_filename.empty()) {
    xml_filename = create_tmp_xml();
  }
  wbc::BranchingRepresentation * brep(wbc::BRParser::parse("sai", xml_filename));
  return brep;
}


jspace::Model * create_model() throw(runtime_error)
{
  jspace::Model * model(new jspace::Model(new wbc::RobotControlModel(create_brep()), true));
  return model;
}
