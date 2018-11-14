// Include our own header first
#include "RecoLocalTracker/SiPixelRecHits/interface/PixelCPEClusterRepair.h"

// Geometry services
#include "Geometry/TrackerGeometryBuilder/interface/PixelGeomDetUnit.h"
#include "Geometry/TrackerGeometryBuilder/interface/RectangularPixelTopology.h"

// MessageLogger
#include "FWCore/MessageLogger/interface/MessageLogger.h"

// Magnetic field
#include "MagneticField/Engine/interface/MagneticField.h"


// Commented for now (3/10/17) until we figure out how to resuscitate 2D template splitter
/// #include "RecoLocalTracker/SiPixelRecHits/interface/SiPixelTemplateSplit.h"

#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include <vector>
#include "boost/multi_array.hpp"

#include <iostream>

#include "TFile.h"
#include "TTree.h"

#define PRINT 0

using namespace SiPixelTemplateReco;
//using namespace SiPixelTemplateSplit;
using namespace std;

namespace {
   constexpr float micronsToCm = 1.0e-4;
   constexpr int cluster_matrix_size_x = 13;
   constexpr int cluster_matrix_size_y = 21;
}

//-----------------------------------------------------------------------------
//  Constructor.
//
//-----------------------------------------------------------------------------
PixelCPEClusterRepair::PixelCPEClusterRepair(edm::ParameterSet const & conf,
                                           const MagneticField * mag,
                                           const TrackerGeometry& geom,
                                           const TrackerTopology& ttopo,
                                           const SiPixelLorentzAngle * lorentzAngle,
					   const SiPixelTemplateDBObject * templateDBobject,
					   const SiPixel2DTemplateDBObject * templateDBobject2D )
: PixelCPEBase(conf, mag, geom, ttopo, lorentzAngle, nullptr, templateDBobject, nullptr,1)
{
   LogDebug("PixelCPEClusterRepair::(constructor)") << endl;

   //--- Parameter to decide between DB or text file template access
   if ( LoadTemplatesFromDB_ )
   {
      // Initialize template store to the selected ID [Morris, 6/25/08]
      if ( !SiPixelTemplate::pushfile( *templateDBobject_, thePixelTemp_) )
         throw cms::Exception("PixelCPEClusterRepair")
         << "\nERROR: Templates not filled correctly. Check the sqlite file. Using SiPixelTemplateDBObject version "
         << (*templateDBobject_).version() << "\n\n";

      // Initialize template store to the selected ID [Morris, 6/25/08]
      if ( !SiPixelTemplate2D::pushfile( *templateDBobject2D , thePixelTemp2D_) )
         throw cms::Exception("PixelCPEClusterRepair")
         << "\nERROR: Templates not filled correctly. Check the sqlite file. Using SiPixelTemplateDBObject version "
         << (*templateDBobject_).version() << "\n\n";
   }
   else
   {
      LogDebug("PixelCPEClusterRepair") << "Loading templates for barrel and forward from ASCII files." << endl;
      //--- (Archaic) Get configurable template IDs.  This code executes only if we loading pixels from ASCII
      //    files, and then they are mandatory.
      barrelTemplateID_  = conf.getParameter<int>( "barrelTemplateID" );
      forwardTemplateID_ = conf.getParameter<int>( "forwardTemplateID" );
      templateDir_       = conf.getParameter<int>( "directoryWithTemplates" );
     
      if ( !SiPixelTemplate::pushfile( barrelTemplateID_  , thePixelTemp_ , templateDir_ ) )
         throw cms::Exception("PixelCPEClusterRepair")
	 << "\nERROR: Template ID " << barrelTemplateID_ << " not loaded correctly from text file. Reconstruction will fail.\n\n";
      
      if ( !SiPixelTemplate::pushfile( forwardTemplateID_ , thePixelTemp_ , templateDir_ ) )
         throw cms::Exception("PixelCPEClusterRepair")
	 << "\nERROR: Template ID " << forwardTemplateID_ << " not loaded correctly from text file. Reconstruction will fail.\n\n";
   }
   
   speed_ = conf.getParameter<int>( "speed");
   LogDebug("PixelCPEClusterRepair::PixelCPEClusterRepair:") <<
   "Template speed = " << speed_ << "\n";
   
   UseClusterSplitter_ = conf.getParameter<bool>("UseClusterSplitter");   


   //--- Configure 2D reco.
   if ( conf.exists("MinProbY") )
     minProbY_ = conf.getParameter<double>("MinProbY");
   else
     minProbY_ = 0.001;           // probabilityY < 0.001

   if ( conf.exists("MaxSizeMismatchInY") )
     maxSizeMismatchInY_ = conf.getParameter<int>("MaxSizeMismatchInY");
   else
     maxSizeMismatchInY_ = 1;     // ( templ.clsleny() - nypix > 1)

}



//-----------------------------------------------------------------------------
//  Clean up.
//-----------------------------------------------------------------------------
PixelCPEClusterRepair::~PixelCPEClusterRepair()
{
   for (auto x : thePixelTemp_)   x.destroy();
   for (auto x : thePixelTemp2D_) x.destroy();
}

PixelCPEBase::ClusterParam* PixelCPEClusterRepair::createClusterParam(const SiPixelCluster & cl) const
{
   return new ClusterParamTemplate(cl);
}



//------------------------------------------------------------------
//  Public methods mandated by the base class.
//------------------------------------------------------------------

//------------------------------------------------------------------
//  The main call to the template code.
//------------------------------------------------------------------
LocalPoint
PixelCPEClusterRepair::localPosition(DetParam const & theDetParam, ClusterParam & theClusterParamBase) const
{
   
   ClusterParamTemplate & theClusterParam = static_cast<ClusterParamTemplate &>(theClusterParamBase);
   bool filled_from_2d = false;
   
   if(!GeomDetEnumerators::isTrackerPixel(theDetParam.thePart))
      throw cms::Exception("PixelCPEClusterRepair::localPosition :")
      << "A non-pixel detector type in here?";
   
   int ID = -9999;
   if ( LoadTemplatesFromDB_ ) {
      int ID0 = templateDBobject_->getTemplateID(theDetParam.theDet->geographicalId()); // just to comapre
      ID = theDetParam.detTemplateId;
      if(ID0!=ID) edm::LogError("PixelCPEClusterRepair") <<" different id"<< ID<<" "<<ID0<<endl;
   } else { // from asci file
      if ( ! GeomDetEnumerators::isEndcap(theDetParam.thePart) )
	ID = barrelTemplateID_  ; // barrel
      else
	ID = forwardTemplateID_ ; // forward
   }
   //cout << "PixelCPEClusterRepair : ID = " << ID << endl;
   //

   // &&& PM, note for later: PixelCPEBase calculates minInX,Y, and maxInX,Y
   //     Why can't we simply use that and save time with row_offset, col_offset
   //     and mrow = maxInX-minInX, mcol = maxInY-minInY ... Except that we
   //     also need to take into account cluster_matrix_size_x,y.

   
   //--- Preparing to retrieve ADC counts from the SiPixeltheClusterParam.theCluster
   //    The pixels from minPixelRow() will go into clust_array_2d[0][*],
   //    and the pixels from minPixelCol() will go into clust_array_2d[*][0].
   int row_offset = theClusterParam.theCluster->minPixelRow();
   int col_offset = theClusterParam.theCluster->minPixelCol();
   
   //--- Store the coordinates of the center of the (0,0) pixel of the array that
   //    gets passed to PixelTempReco2D.  Will add these values to the output of TemplReco2D
   float tmp_x = float(row_offset) + 0.5f;
   float tmp_y = float(col_offset) + 0.5f;

   
   //--- Store these offsets (to be added later) in a LocalPoint after tranforming
   //    them from measurement units (pixel units) to local coordinates (cm)
   LocalPoint lp;
   if ( theClusterParam.with_track_angle )
      //--- Update call with trk angles needed for bow/kink corrections  // Gavril
      lp = theDetParam.theTopol->localPosition( MeasurementPoint(tmp_x, tmp_y), theClusterParam.loc_trk_pred );
   else
   {
      edm::LogError("PixelCPEClusterRepair")
      << "@SUB = PixelCPEClusterRepair::localPosition"
      << "Should never be here. PixelCPEClusterRepair should always be called with track angles. This is a bad error !!! ";
      lp = theDetParam.theTopol->localPosition( MeasurementPoint(tmp_x, tmp_y) );
   }

   
   //--- Compute the size of the matrix which will be passed to TemplateReco.
   //    We'll later make  clustMatrix[ mrow ][ mcol ]
   int mrow=0, mcol=0;
   for (int i=0 ; i!=theClusterParam.theCluster->size(); ++i )
   {
      auto pix = theClusterParam.theCluster->pixel(i);
      int irow = int(pix.x);
      int icol = int(pix.y);
      mrow = std::max(mrow,irow);
      mcol = std::max(mcol,icol);
   }
   mrow -= row_offset; mrow+=1; mrow = std::min(mrow,cluster_matrix_size_x);
   mcol -= col_offset; mcol+=1; mcol = std::min(mcol,cluster_matrix_size_y);
   assert(mrow>0); assert(mcol>0);


   //--- Make and fill the bool arrays flagging double pixels
   bool xdouble[mrow], ydouble[mcol];
   // x directions (shorter), rows
   for (int irow = 0; irow < mrow; ++irow)
      xdouble[irow] = theDetParam.theRecTopol->isItBigPixelInX( irow+row_offset );
   //
   // y directions (longer), columns
   for (int icol = 0; icol < mcol; ++icol)
      ydouble[icol] = theDetParam.theRecTopol->isItBigPixelInY( icol+col_offset );

   //--- C-style matrix.  We'll need it in either case.
   float clustMatrix[mrow][mcol];
   float clustMatrix2[mrow][mcol];


   float localx_1d(0.), localy_1d(0.);

   int fail_mode = 0;

   //--- Prepare struct that passes pointers to TemplateReco.  It doesn't own anything.
   SiPixelTemplateReco::ClusMatrix   clusterPayload  { &clustMatrix[0][0], xdouble, ydouble, mrow,mcol};
   SiPixelTemplateReco2D::ClusMatrix clusterPayload2d{ &clustMatrix2[0][0], xdouble, ydouble, mrow,mcol};


   //--- Copy clust's pixels (calibrated in electrons) into clustMatrix;
   memset( clustMatrix, 0, sizeof(float)*mrow*mcol );   // Wipe it clean.
   for (int i=0 ; i!=theClusterParam.theCluster->size(); ++i )
   {
       auto pix = theClusterParam.theCluster->pixel(i);
       int irow = int(pix.x) - row_offset;
       int icol = int(pix.y) - col_offset;
       // &&& Do we ever get pixels that are out of bounds ???  Need to check.
       if ( (irow<mrow) & (icol<mcol) ) clustMatrix[irow][icol] =  float(pix.adc);

       //kill pixels at start of cluster
       /*
       if(col_offset %2 == 0){
           //cluster starts at beginning of double column, kill first two cols
           //if large enough
           if ( (irow<mrow) && (icol<mcol) && (mcol > 2) && (icol ==0 || icol==1)){ 
               clustMatrix[irow][icol] = 0;
               fail_mode = 2;
           }
       }
       else{
           //cluster starts at 2nd pixel in a double column, kill only first col
           //if large enough
           if ( (irow<mrow) && (icol<mcol) && (mcol > 1) && (icol ==0)){ 
               clustMatrix[irow][icol] = 0;
               fail_mode = 1;
           }
       }
       */
   }


   // &&& Save for later: fillClustMatrix( float * clustMatrix );

   //--- Save a copy of clustMatrix into clustMatrix2
   memcpy( clustMatrix2, clustMatrix, sizeof(float)*mrow*mcol);

   //--- Set both return statuses, since we may call only one.
   theClusterParam.ierr  = 0;
   theClusterParam.ierr2 = 0;



   if(PRINT) printf("123CRTEST456 \n");
   //--- Are we on edge?
   if ( theClusterParam.isOnEdge_ ) {
     //--- Call the Template Reco 2d with cluster repair.0
     filled_from_2d = true;
     callTempReco2D( theDetParam, theClusterParam, clusterPayload2d, ID, lp );
     if(PRINT) printf("nydiff=%.2f proby1d=%.2e qratio=%.3f \n", 0., 0., 1.0);
   }
   else {
     //theClusterParam.recommended2D_ = false;
     //--- Call the vanilla Template Reco
     callTempReco1D( theDetParam, theClusterParam, clusterPayload, ID, lp );

     //--- Did we find a cluster which has bad probability and not enough charge?
     if ( theClusterParam.recommended2D_) {
       // printf("ClusterRepair calling 2D! \n");
       //--- Yes. So run Template Reco 2d with cluster repair.
       

       //--- Call the Template Reco 2d with cluster repair
       localx_1d = theClusterParam.templXrec_;
       localy_1d = theClusterParam.templYrec_;
       callTempReco2D( theDetParam, theClusterParam, clusterPayload2d, ID, lp );
       filled_from_2d = true;
     }

   }

   //--- Make sure cluster repair returns all info about the hit back up to caller
   //--- Necessary because it copied the base class so it does not modify it
   theClusterParamBase.isOnEdge_ = theClusterParam.isOnEdge_;
   theClusterParamBase.hasBadPixels_ = theClusterParam.hasBadPixels_;
   theClusterParamBase.spansTwoROCs_ = theClusterParam.spansTwoROCs_;
   theClusterParamBase.hasFilledProb_ = theClusterParam.hasFilledProb_;
   theClusterParamBase.qBin_ = theClusterParam.qBin_;
   theClusterParamBase.probabilityQ_ = theClusterParam.probabilityQ_;
   theClusterParamBase.filled_from_2d = filled_from_2d;
   theClusterParamBase.edgeTypeY_ = theClusterParam.edgeTypeY_;
   if(filled_from_2d){
       theClusterParamBase.probabilityX_ = theClusterParam.templProbXY_;
       theClusterParamBase.probabilityY_ = 0.;
    }
   else{
       theClusterParamBase.probabilityX_ = theClusterParam.probabilityX_;
       theClusterParamBase.probabilityY_ = theClusterParam.probabilityY_;
   }

   
   float ret_x, ret_y;
   if(isnan(theClusterParam.templXrec_) || isnan( theClusterParam.templYrec_)){
       ret_x = localx_1d;
       ret_y = localy_1d;
   }
   else{
       ret_x = theClusterParam.templXrec_;
       ret_y = theClusterParam.templYrec_;
   }

   if(PRINT){
       printf("fail_mode=%i, on_edge=%i, used_2d=%i, spans_two_ROCs=%i, detID=%i \n",
               fail_mode, theClusterParam.isOnEdge_, filled_from_2d, theClusterParam.spansTwoROCs_, theDetParam.detTemplateId);
       printf("Local X, Local Y = %.5f, %.5f \n", ret_x, ret_y);
       if(filled_from_2d && !theClusterParam.isOnEdge_)
            printf("1D: X,Y = %.5f, %.5f \n",localx_1d, localy_1d);
       else
            printf("1D: X,Y = %.5f, %.5f \n", theClusterParam.templXrec_, theClusterParam.templYrec_);
       printf("CR: X,Y = %.5f, %.5f \n", theClusterParam.templXrec_, theClusterParam.templYrec_);
   }


   return LocalPoint( ret_x, ret_y );
}


//------------------------------------------------------------------
//  Helper function to aggregate call & handling of Template Reco
//------------------------------------------------------------------
void 
PixelCPEClusterRepair::callTempReco1D( DetParam const & theDetParam, 
				       ClusterParamTemplate & theClusterParam,
				       SiPixelTemplateReco::ClusMatrix & clusterPayload,
				       int ID, LocalPoint & lp ) const
{
   SiPixelTemplate templ(thePixelTemp_);
   
   // Output:
   float nonsense = -99999.9f; // nonsense init value
   theClusterParam.templXrec_ = theClusterParam.templYrec_ = theClusterParam.templSigmaX_ = theClusterParam.templSigmaY_ = nonsense;
   // If the template recontruction fails, we want to return 1.0 for now
   theClusterParam.probabilityX_ = theClusterParam.probabilityY_ = theClusterParam.probabilityQ_ = 1.f;
   theClusterParam.qBin_ = 0;
   // We have a boolean denoting whether the reco failed or not
   theClusterParam.hasFilledProb_ = false;


   
   // ******************************************************************
   //--- Call normal TemplateReco
   //
   float locBz = theDetParam.bz;
   float locBx = theDetParam.bx;
   //
   const bool deadpix = false;
   std::vector<std::pair<int, int> > zeropix;
   int nypix =0, nxpix = 0;
   //
   theClusterParam.ierr =
   PixelTempReco1D( ID, theClusterParam.cotalpha, theClusterParam.cotbeta,
                   locBz, locBx,
                   clusterPayload,
                   templ,
                   theClusterParam.templYrec_, theClusterParam.templSigmaY_, theClusterParam.probabilityY_,
                   theClusterParam.templXrec_, theClusterParam.templSigmaX_, theClusterParam.probabilityX_,
                   theClusterParam.qBin_,
                   speed_, deadpix, zeropix,
                   theClusterParam.probabilityQ_, nypix, nxpix
                   );
   // ******************************************************************
   
   //--- Check exit status
   if UNLIKELY( theClusterParam.ierr != 0 )
   {
      LogDebug("PixelCPEClusterRepair::localPosition") <<
      "reconstruction failed with error " << theClusterParam.ierr << "\n";

      theClusterParam.probabilityX_ = theClusterParam.probabilityY_ = theClusterParam.probabilityQ_ = 0.f;
      theClusterParam.qBin_ = 0;
      
      // Gavril: what do we do in this case ? For now, just return the cluster center of gravity in microns
      // In the x case, apply a rough Lorentz drift average correction
      // To do: call PixelCPEGeneric whenever PixelTempReco1D fails
      float lorentz_drift = -999.9;
      if ( ! GeomDetEnumerators::isEndcap(theDetParam.thePart) )
         lorentz_drift = 60.0f; // in microns
      else
         lorentz_drift = 10.0f; // in microns
      // GG: trk angles needed to correct for bows/kinks
      if ( theClusterParam.with_track_angle )
      {
         theClusterParam.templXrec_ = theDetParam.theTopol->localX( theClusterParam.theCluster->x(), theClusterParam.loc_trk_pred ) - lorentz_drift * micronsToCm; // rough Lorentz drift correction
         theClusterParam.templYrec_ = theDetParam.theTopol->localY( theClusterParam.theCluster->y(), theClusterParam.loc_trk_pred );
      }
      else
      {
         edm::LogError("PixelCPEClusterRepair")
         << "@SUB = PixelCPEClusterRepair::localPosition"
         << "Should never be here. PixelCPEClusterRepair should always be called with track angles. This is a bad error !!! ";
         
         theClusterParam.templXrec_ = theDetParam.theTopol->localX( theClusterParam.theCluster->x() ) - lorentz_drift * micronsToCm; // rough Lorentz drift correction
         theClusterParam.templYrec_ = theDetParam.theTopol->localY( theClusterParam.theCluster->y() );
      }
   }   
   else 
   {
      //--- Template Reco succeeded.  The probabilities are filled.
      theClusterParam.hasFilledProb_ = true;

      //--- templ.clsleny() is the expected length of the cluster along y axis.
      //--- If the fit is poor and cluster is shorter than expected, possibly
      //    due to truncated cluster, so try 2D reco
      float totCharge = 0.;
      for(int k = 0; k<clusterPayload.mrow * clusterPayload.mcol; k++){
          totCharge += clusterPayload.matrix[k];
      }
      Double_t nydiff = templ.clsleny() - nypix;
      Double_t qratio = totCharge/templ.qavg();

      //if ( (nydiff > 0.1) || (qratio < 0.6) || (theClusterParam.probabilityY_ < 0.15)){
      if ( (nydiff > 0.5) || (qratio < 0.5)){
          theClusterParam.recommended2D_ = true;
          theClusterParam.hasBadPixels_ = true;
          if(PRINT) printf("nydiff=%.2f proby1d=%.2e qratio=%.3f \n", templ.clsleny() - nypix, theClusterParam.probabilityY_, totCharge/templ.qavg());
          // Truncated clusters usually come from stuck TBMs which kill entire
          // double columns

          // Cluster is of even length, so either both or neither ends, end on
          // a double column, so we cannot figure out the likely edge of
          // truncation, let the 2D algorithm try extending on both sides (option 3)
          if(theClusterParam.theCluster->sizeY() % 2 == 0) theClusterParam.edgeTypeY_ = 3;

          else{
              //The cluster is of odd length, only one of the edges can end on
              //a double column, this is the likely edge of truncation
              //Double columns always start on even indexes

              int min_col = theClusterParam.theCluster->minPixelCol();

              if(min_col %2 ==0){
                  //begining edge is at a double column (end edge cannot be,
                  //because odd length) so likely truncated at small y (option 1) 
                  theClusterParam.edgeTypeY_ = 1;
                  //try doing it backwards?
                  //theClusterParam.edgeTypeY_ = 2;
              }
              else{ 
                  //end edge is at a double column (beginning edge cannot be,
                  //because odd length) so likely truncated at large y (option 2) 
                  theClusterParam.edgeTypeY_ = 2;
                  //try doing it backwards?
                  //theClusterParam.edgeTypeY_ = 1;
              }
          }
      }
      
      //--- Go from microns to centimeters
      theClusterParam.templXrec_ *= micronsToCm;
      theClusterParam.templYrec_ *= micronsToCm;
      
      //--- Go back to the module coordinate system
      theClusterParam.templXrec_ += lp.x();
      theClusterParam.templYrec_ += lp.y();
            
   }
   return;
}




//------------------------------------------------------------------
//  Helper function to aggregate call & handling of Template 2D fit
//------------------------------------------------------------------
void 
PixelCPEClusterRepair::callTempReco2D( DetParam const & theDetParam, 
				       ClusterParamTemplate & theClusterParam,
				       SiPixelTemplateReco2D::ClusMatrix & clusterPayload,
				       int ID, LocalPoint & lp ) const
{
   SiPixelTemplate2D templ2d(thePixelTemp2D_);
   
   // Output:
   float nonsense = -99999.9f; // nonsense init value
   theClusterParam.templXrec_ = theClusterParam.templYrec_ = theClusterParam.templSigmaX_ = theClusterParam.templSigmaY_ = nonsense;
   // If the template recontruction fails, we want to return 1.0 for now
   theClusterParam.probabilityX_ = theClusterParam.probabilityY_ = theClusterParam.probabilityQ_ = 1.f;
   theClusterParam.qBin_ = 0;
   // We have a boolean denoting whether the reco failed or not
   theClusterParam.hasFilledProb_ = false;

   // Total charge in the cluster, used to make sure cluster is above readout
   // threshold before passing to 2D Reco
   float totCharge = 0.;
   for(int k = 0; k<clusterPayload.mrow * clusterPayload.mcol; k++){
       totCharge += clusterPayload.matrix[k];
   }
   
   // ******************************************************************
   //--- Call 2D TemplateReco
   //
   float locBz = theDetParam.bz;
   float locBx = theDetParam.bx;

   //--- Input:
   //   edgeflagy - (input) flag to indicate the present of edges in y: 
   //           0=none (or interior gap),1=edge at small y, 2=edge at large y, 3=edge at either end
   //           edgeTypeY is either set by CPEBase for actual detector edges, or guessed
   //           by call1DReco when trying to fix dead double columns
   //
   //   edgeflagx - (input) flag to indicate the present of edges in x: 
   //           0=none, 1=edge at small x, 2=edge at large x
   //
   //   These two variables are calculated in setTheClu() and stored in edgeTypeX_ and edgeTypeY_
   //
   //--- Output:
   //   deltay - (output) template y-length - cluster length [when > 0, possibly missing end]
   //   npixels - ???     &&& Ask Morris


   float deltay = 0;    // return param
   int npixels = 0;     // return param
   constexpr float minReadoutCharge = 4000.; //minimum charge of readout threshold, required for 2D to converge

   if(clusterPayload.mrow > 4){
       // The cluster is too big, the 2D reco will perform horribly.
       // Better to return immediately in error
       theClusterParam.ierr2 = 8;

   }
   /*
   else if(totCharge < minReadoutCharge){
       //there is not enough charge in the cluster (ie it is below readout
       //threshold) This is likely due to some simulation of dead pixels
       //2D won't converge, return error
       //printf("below thresh: totCharge = %.0f clusterChg = %i \n", totCharge, theClusterParam.theCluster->charge() );
       theClusterParam.ierr2 = 6;
   }
   */

   else{
       theClusterParam.ierr2 =
       PixelTempReco2D( ID, theClusterParam.cotalpha, theClusterParam.cotbeta,
                       locBz, locBx,
                       theClusterParam.edgeTypeY_ , theClusterParam.edgeTypeX_ ,
                       clusterPayload,
                       templ2d,
                       theClusterParam.templYrec_, theClusterParam.templSigmaY_, 
                       theClusterParam.templXrec_, theClusterParam.templSigmaX_, 
                       theClusterParam.templProbXY_,
                       theClusterParam.probabilityQ_,
                       theClusterParam.qBin_,
                       deltay, npixels
                       );
   }
   // ******************************************************************
   if (isnan(theClusterParam.templXrec_) || isnan(theClusterParam.templYrec_)){
       printf("NAN RETURNING FROM 2D!! \n");
       printf("cotalpha, cotbeta = %.4f %.4f \n", theClusterParam.cotalpha, theClusterParam.cotbeta);
   }

   
   //--- Check exit status
   if UNLIKELY( theClusterParam.ierr2 != 0 )
   {
      //printf("2D RECO had error %i \n", theClusterParam.ierr2);
      LogDebug("PixelCPEClusterRepair::localPosition") <<
      "2D reconstruction failed with error " << theClusterParam.ierr2 << "\n";
      
      theClusterParam.probabilityX_ = theClusterParam.probabilityY_ = theClusterParam.probabilityQ_ = 0.f;
      theClusterParam.qBin_ = 0;
      // GG: what do we do in this case?  For now, just return the cluster center of gravity in microns
      // In the x case, apply a rough Lorentz drift average correction
      float lorentz_drift = -999.9;
      if ( ! GeomDetEnumerators::isEndcap(theDetParam.thePart) )
         lorentz_drift = 60.0f; // in microns  // &&& replace with a constant (globally)
      else
         lorentz_drift = 10.0f; // in microns
      // GG: trk angles needed to correct for bows/kinks
      if ( theClusterParam.with_track_angle )
      {
         theClusterParam.templXrec_ = theDetParam.theTopol->localX( theClusterParam.theCluster->x(), theClusterParam.loc_trk_pred ) - lorentz_drift * micronsToCm; // rough Lorentz drift correction
         theClusterParam.templYrec_ = theDetParam.theTopol->localY( theClusterParam.theCluster->y(), theClusterParam.loc_trk_pred );
      }
      else
      {
         edm::LogError("PixelCPEClusterRepair")
         << "@SUB = PixelCPEClusterRepair::localPosition"
         << "Should never be here. PixelCPEClusterRepair should always be called with track angles. This is a bad error !!! ";
         
         theClusterParam.templXrec_ = theDetParam.theTopol->localX( theClusterParam.theCluster->x() ) - lorentz_drift * micronsToCm; // rough Lorentz drift correction
         theClusterParam.templYrec_ = theDetParam.theTopol->localY( theClusterParam.theCluster->y() );
      }
   }   
   else 
   {
      //--- Template Reco succeeded.
      theClusterParam.hasFilledProb_ = true;

      //--- Go from microns to centimeters
      theClusterParam.templXrec_ *= micronsToCm;
      theClusterParam.templYrec_ *= micronsToCm;
      
      //--- Go back to the module coordinate system
      theClusterParam.templXrec_ += lp.x();
      theClusterParam.templYrec_ += lp.y();
   }
   return;
}




//------------------------------------------------------------------
//  localError() relies on localPosition() being called FIRST!!!
//------------------------------------------------------------------
LocalError
PixelCPEClusterRepair::localError(DetParam const & theDetParam,  ClusterParam & theClusterParamBase) const
{
   
   ClusterParamTemplate & theClusterParam = static_cast<ClusterParamTemplate &>(theClusterParamBase);
   
   //--- Default is the maximum error used for edge clusters.
   //--- (never used, in fact: let comment it out, shut up the complains of the static analyzer, and save a few CPU cycles)
   float xerr = 0.0f, yerr = 0.0f;

   //--- Check status of both template calls.
   if UNLIKELY ( (theClusterParam.ierr !=0) || (theClusterParam.ierr2 !=0) ) {
     // If reconstruction fails the hit position is calculated from cluster center of gravity
     // corrected in x by average Lorentz drift. Assign huge errors.
     //
     if UNLIKELY (!GeomDetEnumerators::isTrackerPixel(theDetParam.thePart))
       throw cms::Exception("PixelCPEClusterRepair::localPosition :")
	 << "A non-pixel detector type in here?";
     
     // Assign better errors based on the residuals for failed template cases
     if ( GeomDetEnumerators::isBarrel(theDetParam.thePart)) {
         xerr = 55.0f * micronsToCm;      // &&& get errors from elsewhere?
	 yerr = 36.0f * micronsToCm;
       }
       else {
	 xerr = 42.0f * micronsToCm;
	 yerr = 39.0f * micronsToCm;
       }
   }
   // Leave commented for now, until we study the interplay of failure modes
   // of 1D template reco and edges.  For edge hits we run 2D reco by default!
   //
   // else if ( theClusterParam.edgeTypeX_ || theClusterParam.edgeTypeY_ )  {
   //   // for edge pixels assign errors according to observed residual RMS
   //   if      ( theClusterParam.edgeTypeX_ && !theClusterParam.edgeTypeY_ ) {
   //     xerr = 23.0f * micronsToCm;
   //     yerr = 39.0f * micronsToCm;
   //   }
   //   else if ( !theClusterParam.edgeTypeX_ && theClusterParam.edgeTypeY_ ) {
   //     xerr = 24.0f * micronsToCm;
   //     yerr = 96.0f * micronsToCm;
   //   }
   //   else if ( theClusterParam.edgeTypeX_ && theClusterParam.edgeTypeY_ ) {
   //     xerr = 31.0f * micronsToCm;
   //     yerr = 90.0f * micronsToCm;
   //   }
   // }
   else {
     xerr = theClusterParam.templSigmaX_ * micronsToCm;
     yerr = theClusterParam.templSigmaY_ * micronsToCm;
     // &&& should also check ierr (saved as class variable) and return
     // &&& nonsense (another class static) if the template fit failed.
   }       
   
   if (theVerboseLevel > 9) {
     LogDebug("PixelCPEClusterRepair") 
       << " Sizex = " << theClusterParam.theCluster->sizeX() 
       << " Sizey = " << theClusterParam.theCluster->sizeY() 
       << " Edgex = " << theClusterParam.edgeTypeX_ 
       << " Edgey = " << theClusterParam.edgeTypeY_ 
       << " ErrX  = " << xerr << " ErrY  = " << yerr;
   }
   
   
   if ( !(xerr > 0.0f) )
      throw cms::Exception("PixelCPEClusterRepair::localError") 
      << "\nERROR: Negative pixel error xerr = " << xerr << "\n\n";
   
   if ( !(yerr > 0.0f) )
      throw cms::Exception("PixelCPEClusterRepair::localError") 
      << "\nERROR: Negative pixel error yerr = " << yerr << "\n\n";
   
   return LocalError(xerr*xerr, 0, yerr*yerr);
}

