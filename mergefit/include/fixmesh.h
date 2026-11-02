
#ifndef __FIXMESH_H
#define __FIXMESH_H
#include <stdio.h>
#include "load.h"

using namespace std;



class fIxmesh {
public:

	void Fixmesh(load& mRes, int fix_hole, int local_layer, int iteration);
	void computeboundary(Eigen::MatrixXd ver, Eigen::MatrixXi fac, Eigen::VectorXi &e2e);
	void change_normal(load& mRes, vector<int> f);
};

#endif
