/*
 * Copyright (C) 2016 iCub Facility - Istituto Italiano di Tecnologia
 * Author: Matej Hoffmann
 * email:  matej.hoffmann@iit.it
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
*/

/** 
\defgroup ppsAggregEventsForiCubGui ppsAggregEventsForiCubGui
 
Transforms aggregated tactile/peripersonal space events, as generated by skinEventsAggegator or visuoTactileRF, into a dynContact output that can be sent to/iCubGui/forces and visualized. 
 
\section intro_sec Description 

Transforms aggregated tactile/peripersonal space events, as generated by skinEventsAggegator or visuoTactileRF, into a dynContact output that can be sent to/iCubGui/forces and visualized. 
The input has the following format: aggregated output per skin part with average location, normal and magnitude as extracted from the skin positions files. 
Maximum 1 vector per skin part; format: (SkinPart_enum x y z o1 o2 o3 magnitude SkinPart_string) (...) - for a maximum of the number of skin parts active.
 
\section lib_sec Libraries 
- YARP.
- skinDynLib 

\section parameters_sec Parameters

--context    \e path
- Where to find the called resource.

--from       \e from
- The name of the .ini file with the configuration parameters.

--name       \e name
- The name of the module.

--verbosity  \e verb
- Verbosity level (default 0). The higher is the verbosity, the more
  information is printed out.

--autoconnect    \e aut
- If to connect automatically to ports. Default not.
 
--tactile \e tac
- if enabled, the tactile aggreg events will be prepared for iCubGui visualization.
 
--pps \e pps
- if enabled, the peripersonal space aggreg events will be prepared for iCubGui visualization.
  
--gain \e gain
- the multiplication vector for the visualization of normalized event magnitude.
 
\section portsc_sec Ports Created 

- <i> /<name>/skin_events_aggreg:i</i> gets the aggregated skin events as produced by the skinEventsAggegator
- <i> /<name>/pps_events_aggreg:i</i> gets the aggregated pps events as produced by the visuoTactileRF

- <i> /<name>/contacts:o </i>  prepares a dynContact that can be visualized using /iCubGui/forces 
\section tested_os_sec Tested OS
Linux (Ubuntu 12.04)
 
\author Matej Hoffmann
*/ 

#include <stdio.h>
#include <cstdio>
#include <string>

#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <yarp/math/Math.h>

#include <iCub/skinDynLib/dynContact.h>
#include <iCub/skinDynLib/dynContactList.h>
#include <iCub/skinDynLib/skinContact.h>
#include <iCub/skinDynLib/skinContactList.h>

using namespace std;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::math;
using namespace iCub::skinDynLib;

/**
* \ingroup ppsAggregEventsForiCubGuiModule
*
* The module that transforms aggregated tactile/peripersonal space events,
* as generated by skinEventsAggegator/visuoTactileRF, into a skinContactList
* output that can be sent to/iCubGui/forces and visualized.
*  
*/
class ppsAggregEventsForiCubGui: public RFModule 
{
private:
    //EXTERNAL VARIABLES
    string context; 
    string from;
    string name;
    int verbosity;
    bool autoconnect; // on | off
    bool tactile; // on | off
    bool pps; // on | off
    double gain;
 
    //INTERNAL VARIABLES
    BufferedPort<Bottle> aggregSkinEventsInPort; //coming from /skinEventsAggregator/skin_events_aggreg:o
    BufferedPort<Bottle> aggregPPSeventsInPort; //coming from visuoTactileRF/pps_activations_aggreg:o 
    //expected format for both: (skinPart_s x y z o1 o2 o3 magnitude), with position x,y,z and normal o1 o2 o3 in link FoR
    BufferedPort<skinContactList> aggregEventsForiCubGuiPort;
    yarp::os::Stamp ts;
    
    skinContactList mySkinContactList;

    // reading modified from react-control reactCtrlThread::getCollisionPointsFromPort
    // writing adapted from iCub_Sim.cpp OdeSdlSimulation::inspectWholeBodyContactsAndSendTouch()
    bool fillSkinContactFromAggregPort(BufferedPort<Bottle> &inPort, const double amplification, skinContactList &sCL)
    {
        SkinPart sp = SKIN_PART_UNKNOWN;
        //all in the link FoR
        Vector geocenter(3,0.0); //geocenter from skin / average activation locus from the pps
        Vector normal(3,0.0);
        Vector force(3,0.0);
        Vector moment(3,0.0); //will remain zero
        double normalized_activation = 0.0;
        std::vector<unsigned int> taxel_list;
        taxel_list.clear(); //we will be always passing empty list

        Bottle* collPointsMultiBottle = inPort.read(false);

        if(collPointsMultiBottle != NULL)
        {
            //printf("fillSkinContactFromAggregPort(): There were %d bottles on the port.\n",collPointsMultiBottle->size());
            for(int i=0; i< collPointsMultiBottle->size();i++)
            {
                sp = SKIN_PART_UNKNOWN;
                geocenter.zero(); normal.zero();  normalized_activation = 0.0;
                Bottle* collPointBottle = collPointsMultiBottle->get(i).asList();
                //printf("Bottle %d contains %s \n", i,collPointBottle->toString().c_str());
                sp =  (SkinPart)(collPointBottle->get(0).asInt());
                geocenter(0) = collPointBottle->get(1).asDouble();
                geocenter(1) = collPointBottle->get(2).asDouble();
                geocenter(2) = collPointBottle->get(3).asDouble();
                normal(0) = collPointBottle->get(4).asDouble();
                normal(1) = collPointBottle->get(5).asDouble();
                normal(2) =  collPointBottle->get(6).asDouble();
                normalized_activation = collPointBottle->get(13).asDouble();

                force(0)=-1.0*amplification*normalized_activation*normal(0); force(1)=-1.0*amplification*normalized_activation*normal(1);
                force(2) = -1.0*amplification*normalized_activation*normal(2);
                
                //see  iCubGui/src/objectsthread.h    ObjectsManager::manage(iCub::skinDynLib::skinContactList &forces)
                //printf("fillDynContactFromAggregPort: setting dynContact: Body part: %s Linknum: %d CoP: %s F: %s M: %s\n",
                //BodyPart_s[SkinPart_2_BodyPart[sp].body].c_str(),getLinkNum(sp),geoCenter.toString(3,3).c_str(),
                //                              (-1.0*normal).toString(3,3).c_str(),moment.toString(3,3).c_str());

                skinContact sc(SkinPart_2_BodyPart[sp].body,sp,getLinkNum(sp),geocenter,geocenter,taxel_list,
                               amplification*normalized_activation,normal,force,moment);
                // In skinManager/src/compensator.cpp, Compensator::getContacts():
                // set an estimate of the force that is with normal direction and intensity equal to the pressure
                // d.setForceModule(-0.05*activeTaxels*pressure*normal);
                sCL.push_back(sc);
            }
            return true;
        }
        else{
            //printf("fillDynContactFromAggregPort(): no tactile/pps vectors on the port.\n") ;
            return false;
        };   
    }

    void sendContacts(BufferedPort<skinContactList> &outPort, const skinContactList &sCL)
    {
        ts.update();
        skinContactList &sCLout = outPort.prepare();
        sCLout.clear();

        sCLout = sCL;
        
        outPort.setEnvelope(ts);
        outPort.write();     
    }

public:
    ppsAggregEventsForiCubGui()
    {
    }

    bool configure(ResourceFinder &rf)
    {
        context=rf.check("context",Value("periPersonalSpace")).asString(); 
        from=rf.check("from",Value("ppsAggregEventsForiCubGui.ini")).asString();
        name=rf.check("name",Value("ppsAggregEventsForiCubGui")).asString();
        verbosity = rf.check("verbosity",Value(0)).asInt();
        autoconnect=rf.check("autoconnect",Value("off")).asString()=="on"?true:false; // on | off
        tactile=rf.check("tactile",Value("on")).asString()=="on"?true:false; // on | off
        pps=rf.check("pps",Value("on")).asString()=="on"?true:false; // on | off
        gain=rf.check("gain",Value(50.0)).asDouble();
    
        yInfo("[ppsAggregEventsForiCubGui] Initial Parameters:");
        yInfo("Context: %s \t From: %s \t Name: %s \t Verbosity: %d",
               context.c_str(),from.c_str(),name.c_str(),verbosity);
        yInfo("Autoconnect : %d \n tactile: %d \n pps: %d \n gain: %f \n",
               autoconnect,tactile,pps,gain);
    
        //open ports 
        if(tactile)
        {
            aggregSkinEventsInPort.open("/"+name+"/skin_events_aggreg:i");
        }
        if(pps)
        {
            aggregPPSeventsInPort.open("/"+name+"/pps_events_aggreg:i");
        }
        aggregEventsForiCubGuiPort.open("/"+name+"/contacts:o");
        
        if (autoconnect)
        {
            Network::connect("/skinEventsAggregator/skin_events_aggreg:o",("/"+name+"/skin_events_aggreg:i").c_str());
            Network::connect("/visuoTactileRF/pps_events_aggreg:o",("/"+name+"/pps_events_aggreg:i").c_str());
            Network::connect(("/"+name+"/contacts:o").c_str(),"/iCubGui/forces");
        }
        
        
        return true;
    }        
    
    bool close()
    {
        yInfo("Stopping ppsAggregEventsForiCubGui module..");
        
        mySkinContactList.clear();
        
        yInfo("Closing ports..\n");
            if (tactile)
            {
                aggregSkinEventsInPort.interrupt();
                aggregSkinEventsInPort.close();
                yInfo("aggregSkinEventsInPort successfully closed");
            } 
            if (pps)
            {
                aggregPPSeventsInPort.interrupt();
                aggregPPSeventsInPort.close();
                yInfo("aggregPPSeventsInPort successfully closed");
            }
            //if (aggregEventsForiCubGuiPort.isOpen())
            //{
                aggregEventsForiCubGuiPort.interrupt();
                aggregEventsForiCubGuiPort.close();
                yInfo("aggregEventsForiCubGuiPort successfully closed");
            //}
       
       
       return true;
    }

    double getPeriod()
    {
        return 0.03;
    }

    bool updateModule()
    {
     
        mySkinContactList.clear();
  
        if(tactile)
            fillSkinContactFromAggregPort(aggregSkinEventsInPort,gain,mySkinContactList);
        if(pps)
            fillSkinContactFromAggregPort(aggregPPSeventsInPort,gain,mySkinContactList);
        
       // if (! (mySkinContactList.))
        sendContacts(aggregEventsForiCubGuiPort,mySkinContactList);
        
        return true;
    }
   
};

//********************************************
int main(int argc, char * argv[])
{
    Network yarp;

    ResourceFinder rf;
    rf.setVerbose(false);
    rf.setDefaultContext("periPersonalSpace");
    rf.setDefaultConfigFile("ppsAggregEventsForiCubGui.ini");
    rf.configure(argc,argv);
   
    if (rf.check("help"))
    {    
        yInfo(" ");
        yInfo("Options:");
        yInfo("   --context     path:  where to find the called resource (default periPersonalSpace).");
        yInfo("   --from        from:  the name of the .ini file (default ppsAggregEventsForiCubGui.ini).");
        yInfo("   --name        name:  the name of the module (default ppsAggregEventsForiCubGui).");
        yInfo("   --verbosity   verbosity:  verbosity level.");
        yInfo("   --autoConnect flag:  if to auto connect the ports or not. Default no.");
        yInfo("   --tactile    flag:  if enabled, the tactile aggreg events will be prepared for iCubGui visualization.");
        yInfo("   --pps       flag:  if enabled, the peripersonal space aggreg events will be prepared for iCubGui visualization.");
        yInfo("   --gain     gain:  the multiplication vector for the visualization of normalized event magnitude.");
        yInfo(" ");
        return 0;
    }

    if (!yarp.checkNetwork())
    {
        yError("No Network!!!");
        return -1;
    }
      
    ppsAggregEventsForiCubGui module;
    return module.runModule(rf);   
}

