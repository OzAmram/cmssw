#include "RecoTrackAccumulator.h"
#include "FWCore/Framework/interface/ConsumesCollector.h"
#include "FWCore/Framework/interface/one/EDProducer.h"

RecoTrackAccumulator::RecoTrackAccumulator(const edm::ParameterSet& conf, edm::one::EDProducerBase& mixMod, edm::ConsumesCollector& iC) :
  InputSignal_(conf.getParameter<edm::InputTag>("InputSignal")),
  InputPileup_(conf.getParameter<edm::InputTag>("InputPileup")),
  GeneralTrackOutput_(conf.getParameter<std::string>("GeneralTrackOutput")),
  HitOutput_(conf.getParameter<std::string>("HitOutput")),
  GeneralTrackExtraOutput_(conf.getParameter<std::string>("GeneralTrackExtraOutput"))
{

  mixMod.produces<reco::TrackCollection>(GeneralTrackOutput_);
  mixMod.produces<TrackingRecHitCollection>(HitOutput_);
  mixMod.produces<reco::TrackExtraCollection>(GeneralTrackExtraOutput_);

  iC.consumes<reco::TrackCollection>(InputSignal_);
  iC.consumes<TrackingRecHitCollection>(InputSignal_);
  iC.consumes<reco::TrackExtraCollection>(InputSignal_);
}
  
RecoTrackAccumulator::~RecoTrackAccumulator() {
    
}
  
void RecoTrackAccumulator::initializeEvent(edm::Event const& e, edm::EventSetup const& iSetup) {
    
  NewTrackList_ = std::auto_ptr<reco::TrackCollection>(new reco::TrackCollection());
  NewHitList_ = std::auto_ptr<TrackingRecHitCollection>(new TrackingRecHitCollection());
  NewTrackExtraList_ = std::auto_ptr<reco::TrackExtraCollection>(new reco::TrackExtraCollection());

  // this is needed to get the ProductId of the TrackExtra and TrackingRecHit collections
  rTrackExtras=const_cast<edm::Event&>( e ).getRefBeforePut<reco::TrackExtraCollection>(GeneralTrackExtraOutput_);
  rHits=const_cast<edm::Event&>( e ).getRefBeforePut<TrackingRecHitCollection>(HitOutput_);

}
  
void RecoTrackAccumulator::accumulate(edm::Event const& e, edm::EventSetup const& iSetup) {
  

  edm::Handle<reco::TrackCollection> tracks;
  edm::Handle<TrackingRecHitCollection> hits;
  edm::Handle<reco::TrackExtraCollection> trackExtras;
  e.getByLabel(InputSignal_, tracks);
  e.getByLabel(InputSignal_, hits);
  e.getByLabel(InputSignal_, trackExtras);

  // Call the templated version that does the same for both signal and pileup events
  accumulateEvent( e, iSetup, tracks, trackExtras, hits );

}

void RecoTrackAccumulator::accumulate(PileUpEventPrincipal const& e, edm::EventSetup const& iSetup, edm::StreamID const&) {

  if (e.bunchCrossing()==0) {
    edm::Handle<reco::TrackCollection> tracks;
    edm::Handle<TrackingRecHitCollection> hits;
    edm::Handle<reco::TrackExtraCollection> trackExtras;
    e.getByLabel(InputPileup_, tracks);
    e.getByLabel(InputPileup_, hits);
    e.getByLabel(InputPileup_, trackExtras);
    
    // Call the templated version that does the same for both signal and pileup events
    accumulateEvent( e, iSetup, tracks, trackExtras, hits );

  }
}

void RecoTrackAccumulator::finalizeEvent(edm::Event& e, const edm::EventSetup& iSetup) {
  
  e.put( NewTrackList_, GeneralTrackOutput_ );
  e.put( NewHitList_, HitOutput_ );
  e.put( NewTrackExtraList_, GeneralTrackExtraOutput_ );

}


template<class T> void RecoTrackAccumulator::accumulateEvent(const T& e, edm::EventSetup const& iSetup, edm::Handle<reco::TrackCollection> tracks, edm::Handle<reco::TrackExtraCollection> tracksExtras, edm::Handle<TrackingRecHitCollection> hits) {

  if (tracks.isValid()) {
    for (auto const& track : *tracks) {
      NewTrackList_->push_back(track);
      // track extras:
      auto const& extra = tracksExtras->at(track.extra().key());
      NewTrackExtraList_->emplace_back(extra.outerPosition(), extra.outerMomentum(), extra.outerOk(),
				       extra.innerPosition(),extra.innerMomentum(), extra.innerOk(),
				       extra.outerStateCovariance(), extra.outerDetId(),
				       extra.innerStateCovariance(), extra.innerDetId(),
				       extra.seedDirection(),
				       //If TrajectorySeeds are needed, then their list must be gotten from the
				       // secondary event directly and looked up similarly to TrackExtras.
				       //We can't use a default constructed RefToBase due to a bug in RefToBase
				       // which causes an seg fault when calling isAvailable on a default constructed one.
				       edm::RefToBase<TrajectorySeed>{edm::Ref<std::vector<TrajectorySeed>>{}});
      NewTrackList_->back().setExtra( reco::TrackExtraRef( rTrackExtras, NewTrackExtraList_->size() - 1) );
      //reco::TrackExtra & tx = NewTrackExtraList_->back();
      //tx.setResiduals(track.residuals());
      // rechits:
      auto & newExtra = NewTrackExtraList_->back();
      auto const firstTrackIndex = NewHitList_->size();
      for( trackingRecHit_iterator hit = extra.recHitsBegin(); hit != extra.recHitsEnd(); ++ hit ) {
	NewHitList_->push_back( **hit );
      }
      newExtra.setHits( rHits, firstTrackIndex, NewHitList_->size() - firstTrackIndex);
    }
  }

}
