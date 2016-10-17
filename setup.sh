#========================================================================
# Setup script for 6.823

export MIT6823_HOME=/mit/6.823/Spring16
export SVNROOT=file://${MIT6823_HOME}/svnroot/$USER
export LAB0ROOT=file://${MIT6823_HOME}/svnroot/lab0handout
export LAB1ROOT=file://${MIT6823_HOME}/svnroot/lab1handout
export LAB2ROOT=file://${MIT6823_HOME}/svnroot/lab2handout
export LAB3ROOT=file://${MIT6823_HOME}/svnroot/lab3handout
export LAB4ROOT=file://${MIT6823_HOME}/svnroot/lab4handout

export PIN_HOME=~/pin
export PIN_ROOT=${PIN_HOME}
export PIN_KIT=${PIN_HOME}
export LIBCONFIGPATH=/usr/local/
export PATH=${PIN_HOME}:$PATH

export MACHTYPE=`/bin/athena/machtype`

if [ "$MACHTYPE" == "linux" ]
then

  echo ""
  echo " -----------------------------------------------------------"
  echo " This is an Athena/Linux machine. "
  echo " Setting up 6.823 Spring 2016 tools. "
  echo " -----------------------------------------------------------"
  echo ""

  add svn
  # add perlqt_v3.009

  echo ""

else 
 
  echo ""
  echo " -----------------------------------------------------------"
  echo " This is an Athena/Solaris machine. Please log in to an "
  echo " Athena/Linux machine"
  echo " -----------------------------------------------------------"
  echo ""

fi
