/* ------------------------------------------------------------------------
DYSCO VERSION 3.0

ASPECTS AND FEATURES PRESENT
 * TCP sessions with sequence numbers
 * Dysco setup of a TCP session with subsessions
 * Dysco teardown of a TCP session with subsessions
 * transparent middlebox behavior, also initial offsets
 * the locking phase of dynamic reconfiguration, including contention
 * dynamic reconfiguration for the purpose of deletion, insertion,
   replacement, and endpoint mobility, initiated by the left anchor
 * right anchor can request initiation by neighbor, for the purpose of
   mobility
 * middlebox can request initiation by neighbor, for the purpose of
   self-deletion
 * UDP messages can be lost in transit
 * the new path can fail to be set up because a component fails (other 
   component failures are irrelevant)
 * in all protocol states, the Dysco agents anchoring a reconfiguration can
   still send packets through the segment being reconfigured

SIMPLIFYING ASSUMPTIONS
TCP Behavior
 * "addresses" encode both IP addresses and port numbers
 * each data message carries 1 byte of data
 * TCP reset (RST) is not used
 * there is never a "wraparound" of sequence numbers
 * TCP data loss and retransmission is expected and should work, but has
   not yet been modeled
Dysco Behavior
 * selection of addresses is all done manually, on a per-configuration
   basis (if addresses were handled realistically, they would be carried in
   messages, and all messages in the model would have to be considerably
   longer)
 * there is no non-transparent middlebox behavior except for offsets, such
   as compression, decompression, or delaying proxy behavior (note that we
   have modeled all these behaviors in previous versions, without 
   difficulty)
 * a session undergoes at most one reconfiguration (previous versions have
   allowed multiple reconfigurations, and resolved contention among them)
 * once a session has reached readiness for reconfiguration, middleboxes to
   be deleted will never change their deltas
-------------------------------------------------------------------------*/
/* CONSTANTS                                                             */
#define addrSize 8        /* #endpoints = addrSize-2; address 0 reserved */
#define chanSize 5
#define dataLimit 3  /*allows X-1 data messages from right, X-2 from left*/
#define null 0
#define TCP true
#define UDP false
#define leftEnd 1
#define leftMbox 2
#define rightMbox 3
#define leftExtra 4
#define rightExtra 5
#define rightEnd 6
#define mobileAddr 7
mtype = { syn, synAck, ack, synTimeout,              /* message types */
          requestLock, ackLock, nackLock, lockTimeout,
          cancelLock, ackCancel, rightRequest,
          dataAck, finAck,
          unused, endpoint, transparent, offset        /* behavior types */
        } 

/* GLOBAL VARIABLES                                                      */
/* messages (type, protocol, source address, sequence number, ack number)*/
chan addr[addrSize] = [chanSize] of {mtype,bool,short,short,short,short}; 
chan forward[addrSize] =[chanSize] of {mtype,bool,short,short,short,short};
short pair[addrSize];
bool checkpoint = false; /* when true, can check a final state and trace */
       /* checkpoint is optionally set to true in initialization section */
bool requesting[addrSize];

/* MACROS (do not see deltas)                                            */
   inline SendSyn () { 
      if :: behavior == endpoint :: else; sent = newSeq fi;
      sent++; sending = true                              }
   inline ReceiveSyn () { rcvd = newSeq; rcvd++; receiving = true }
   inline SendData () { 
      if :: behavior == endpoint :: else; sent = newSeq fi;
      sent++                                              }
   inline ReceiveData () { 
      if
      :: newSeq >= rcvd; rcvd = newSeq; rcvd++
      :: else               /* races between old and new paths can cause */
      fi    }               /* data to arrive out of order               */
   inline SendFin () { sending = false; finSent = true }
   inline ReceiveFin () { receiving = false; finRcvd = true }
   inline SendAck () {   
      if
      :: behavior == endpoint; rcvdAcked = rcvd
      :: else; rcvdAcked = newAck fi;
      if 
      :: ! receiving && finRcvd && rcvdAcked == rcvd; finRcvd = false 
      :: else fi                                                    }
   inline ReceiveAck () {
      if :: behavior == endpoint; assert (sent >= newAck) :: else fi;
      if 
      :: newAck > sentAcked; sentAcked = newAck
      :: else               /* races between old and new paths can cause */
      fi;                   /* acknowledgments to arrive out of order    */
      if 
      :: ! sending && finSent && sentAcked == sent; finSent = false
      :: else fi                                                  }
   /* the following macros are used in the TwoPaths state,               */
   /* distinguishing old and new paths; for sending data, SendData is    */
   /* used on the new path, and no action is required on the old path    */
   inline ReceiveOldData () {
      if
      :: newSeq >= oldRcvd;
         oldRcvd = newSeq; oldRcvd++;
         if :: rcvd < oldRcvd; rcvd = oldRcvd :: else fi
      :: else fi }          /* races between old and new paths can cause */
   inline ReceiveNewData () {
      if 
      :: (firstNewRcvd == null || newSeq < firstNewRcvd); 
         firstNewRcvd = newSeq 
      :: else fi;
      if
      :: newSeq >= rcvd; rcvd = newSeq; rcvd++
      :: else               /* races between old and new paths can cause */
      fi    }               /* data to arrive out of order               */
   inline SendOldAck (number) {
      oldRcvdAcked = number;
      if
      :: ! receiving && finRcvd &&
         rcvd == oldRcvd &&                    /* no receive on new path */
         oldRcvdAcked == oldRcvd; finRcvd = false
      :: else fi                                }
   inline SendNewAck () {
      if
      :: behavior == endpoint; rcvdAcked = rcvd
      :: else; rcvdAcked = newAck fi;
      if
      :: ! receiving && finRcvd &&
         rcvdAcked == rcvd && oldRcvdAcked == oldRcvd; finRcvd = false
      :: else fi                                                     }
   inline ReceiveOldAck () {
      if
      :: newAck > oldSentAcked; oldSentAcked = newAck
      :: else fi;
      if
      :: ! sending && finSent && oldSentAcked == oldSent &&
         (  sent == oldSent                    /* no sending on new path */
         || sentAcked == sent          /* sending on new path cleaned up */
         ); finSent = false
      :: else fi                                    }
   inline ReceiveNewAck () {
      if :: behavior == endpoint; assert (sent >= newAck) :: else fi;
      if
      :: newAck > sentAcked; sentAcked = newAck
      :: else fi;
      if
      :: ! sending && finSent &&
         sentAcked == sent && oldSentAcked == oldSent; finSent = false
      :: else fi                                                     }

   inline CheckEnding() {
      if :: ! sending && ! receiving && ! finSent && ! finRcvd; goto end
         :: else fi }
   inline CheckCompletion( ) {
      if
      :: oldSentAcked == oldSent && 
         (  (! receiving && ! finRcvd && firstNewRcvd == null) 
                              /* old path terminated, new was never used */
         || (oldRcvd == firstNewRcvd && oldRcvdAcked == oldRcvd)
                              /* all data on old path received and acked */
         );                       
         reconfigured = true; locked = false; otherAnchor = null;
         farLoc = newFarLoc; newFarLoc = null;
         if :: newNearLoc == mobileAddr; pair[mobileAddr] = pair[nearLoc] 
            :: else fi;
         nearLoc = newNearLoc; newNearLoc = null;
         delta = newDelta; newDelta = null;
         oldSent = null; oldSentAcked = null;
         oldRcvd = null; oldRcvdAcked = null; firstNewRcvd = null;
         CheckEnding();
         if :: behavior == endpoint; goto OnePath 
            :: else; goto endOnePath fi
   :: else                                  /* old path still being used */
   fi    }    

/* DECLARE PORT PROCESSES                                                */
proctype portProcess ( short nearLoc ) {
                                                /* NORMAL PROTOCOL STATE */
short farLoc = null;
bool sending = false;
bool finSent = false;
bool receiving = false;
bool finRcvd = false;
                                                      /* NORMAL SN STATE */
short sent = 0;     /* if an endpoint, SN of the next byte it will send; */
                    /* if in a middlebox, SN of next byte to be          */
                    /* forwarded to it by its paired port                */
short sentAcked = 0;         /* SN of next sent byte to be acknowledged, */
                /* so if acks are completely caught up, sent = sentAcked */
short rcvd = 0;                        /* SN of next byte to be received */
short rcvdAcked = 0; /* SN of last received byte acknowledged, plus one; */
                   /* if acks are completely caught up, rcvd = rcvdAcked */
                     /* TEMPORARY VARIABLES FOR RECEIVING MESSAGE FIELDS */
bool proto;
short newSeq, newAck, aux; 
                             /* SPECIAL NON-TRANSPARENT BEHAVIOR, IF ANY */
mtype behavior;
short delta = 0;  
                                          /* STATE VARIABLES FOR LOCKING */
bool reconfigured = false;   /* to limit traces to one reconfig per port */
                                        /* STATE VARIABLES FOR ANCHORING */
bool lockPending = false;
bool locked = false;
short requestor, otherAnchor,
      requestPendingRequestor, requestPendingAnchor, requestPendingDelta;
short newFarLoc, newNearLoc, newDelta; 
short oldSent = 0;      /* in an anchor, the value of sent when old path */
               /* went out of use; hence first byte sent on the new path */
short oldSentAcked = 0;                    /* sentAcked for the old path */
short oldRcvd = 0;/*in an anchor, current guess of other anchor's oldSent*/
short oldRcvdAcked = 0;                    /* rcvdAcked for the old path */
short firstNewRcvd = 0;

/* INITIALIZE PORT PROCESSES                                             */
/* Initialization that does not change for different configurations:     */
/* The initial value of sent is the initial sequence number the process  */
/* will use if it is an endpoint or delaying proxy.                      */
   if
   :: nearLoc == leftEnd; behavior = endpoint; sent = 100
   :: nearLoc == leftMbox; pair[nearLoc] = rightMbox
   :: nearLoc == rightMbox; pair[nearLoc] = leftMbox
   :: nearLoc == leftExtra; pair[nearLoc] = rightExtra
   :: nearLoc == rightExtra; pair[nearLoc] = leftExtra
   :: nearLoc == rightEnd; behavior = endpoint; sent = 600
   fi;
/* Initialize configuration by:                                          */
/* 1) giving each middlebox port process a behavior type;                */
/* 2) giving each port process a farLoc (unless unused); this approach   */
/*    means that messages need not carry supersession information;       */
/* 3) commanding each unused port process to go to end, and each used    */
/*    port process to go to Synchronizing.                               */
/* 4) if you want to see the final state/trace, set checkpoint to true.  */
/*   
   checkpoint = true; 
*/
   if
   :: nearLoc == leftEnd; 
         farLoc = leftMbox;
         addr[farLoc]!syn(TCP,nearLoc,sent,null,null); SendSyn();
         goto endSynchronizing
   :: nearLoc == leftMbox; 
         behavior = offset;
         farLoc = leftEnd;
         goto endSynchronizing
   :: nearLoc == rightMbox;
         behavior = offset;
         farLoc = rightEnd;
         goto endSynchronizing
   :: nearLoc == leftExtra; 
         behavior = offset;
         farLoc = rightMbox;
         goto endSynchronizing
   :: nearLoc == rightExtra; 
         behavior = offset;
         farLoc = rightEnd;
         goto endSynchronizing
   :: nearLoc == rightEnd;
         farLoc = rightMbox;
         goto endSynchronizing
   fi; 
endSynchronizing:  /* BEGIN THREE-WAY HANDSHAKE */
   do
   /* RESPOND TO RECEIVED MESSAGES */
   :: addr[nearLoc]?syn(proto,eval(farLoc),newSeq,null,null); ReceiveSyn();
      if                                   
      :: behavior == endpoint; assert (proto == TCP);
         addr[farLoc]!synAck(TCP,nearLoc,sent+delta,rcvd,null); 
         SendSyn(); SendAck()
      :: behavior != endpoint; 
         forward[pair[nearLoc]]!syn(proto,null,newSeq,null,null)
      :: behavior != endpoint && proto == UDP;     /* inserted box fails */
         addr[farLoc]!synTimeout(UDP,nearLoc,null,null,null);
         goto end
      fi;
   :: addr[nearLoc]?synAck(proto,eval(farLoc),newSeq,newAck,aux);
      newAck = newAck-delta; ReceiveSyn(); ReceiveAck();
      if                           
      :: behavior == endpoint; assert (proto == TCP);
         addr[farLoc]!ack(TCP,nearLoc,null,rcvd,null); 
         SendAck(); goto OnePath 
      :: behavior != endpoint;
         forward[pair[nearLoc]]!synAck(proto,null,newSeq,newAck,aux)
      :: behavior != endpoint && proto == UDP;           /* message lost */
         forward[pair[nearLoc]]!synTimeout(proto,null,null,null,null);
      fi;
   :: addr[nearLoc]?ack(proto,eval(farLoc),newSeq,newAck,aux); 
      newAck = newAck-delta; ReceiveAck();
      if                                   
      :: behavior == endpoint; goto OnePath
      :: else; forward[pair[nearLoc]]!ack(proto,null,newSeq,newAck,aux);
         goto endOnePath
      fi
   :: addr[nearLoc]?synTimeout(UDP,eval(farLoc),null,null,null); 
      forward[pair[nearLoc]]!synTimeout(UDP,null,null,null,null); goto end
   /* RESPOND TO FORWARDED MESSAGES (MIDDLEBOXES ONLY) */
   :: forward[nearLoc]?syn(proto,null,newSeq,null,null); 
      if :: proto == TCP && behavior == offset; delta = 10 :: else fi;
      addr[farLoc]!syn(proto,nearLoc,newSeq+delta,null,null); SendSyn()
   :: forward[nearLoc]?synAck(proto,null,newSeq,newAck,null);
      if :: proto == TCP && behavior == offset; delta = 10 :: else fi;
      addr[farLoc]!synAck(proto,nearLoc,newSeq+delta,newAck,null); 
      SendSyn(); SendAck()
   :: forward[nearLoc]?ack(proto,null,null,newAck,null);  
      addr[farLoc]!ack(proto,nearLoc,null,newAck,null);
      SendAck(); goto endOnePath
   :: forward[nearLoc]?synTimeout(UDP,null,null,null,null);  
      addr[farLoc]!synTimeout(UDP,nearLoc,null,null,null); goto end
   od;
OnePath:  /* NORMAL DATA TRANSMISSION FOR ENDPOINTS */
   do
   /* SEND OR END (ENDPOINTS ONLY) */
   :: sending;
      addr[farLoc]!finAck(TCP,nearLoc,sent+delta,rcvd,null);
      SendFin(); SendData(); SendAck()
   :: nearLoc == leftEnd && sending && (sent < dataLimit+100-1);
      addr[farLoc]!dataAck(TCP,nearLoc,sent+delta,rcvd,null);
      SendData(); SendAck()
   :: nearLoc == rightEnd && sending && (sent < dataLimit+600);
      addr[farLoc]!dataAck(TCP,nearLoc,sent+delta,rcvd,null);
      SendData(); SendAck()
   /* RESPOND TO RECEIVED MESSAGES */
   :: addr[nearLoc]?dataAck(TCP,eval(farLoc),newSeq,newAck,null);
      newAck = newAck-delta; ReceiveData(); ReceiveAck();
      addr[farLoc]!ack(TCP,nearLoc,null,rcvd,null); SendAck()
   :: addr[nearLoc]?finAck(TCP,eval(farLoc),newSeq,newAck,null);
      newAck = newAck-delta;
      ReceiveFin(); ReceiveData(); ReceiveAck();
      if
      :: sending;                             /* this endpoint ends also */
         addr[farLoc]!finAck(TCP,nearLoc,sent+delta,rcvd,null);
         SendFin(); SendData(); SendAck()
      :: sending || ! sending;                 /* endpoint is not ending */
         addr[farLoc]!ack(TCP,nearLoc,null,rcvd,null); SendAck()
      fi;
      CheckEnding()
   :: addr[nearLoc]?ack(TCP,eval(farLoc),newSeq,newAck,null);
      newAck = newAck-delta; ReceiveAck(); CheckEnding()
   /* LOCKING PROTOCOL FOR RECONFIGURATION, ENDPOINT COPY */
   :: ! reconfigured && nearLoc == leftEnd &&        /*CHOOSE LEFT ANCHOR*/
      (sending || receiving) &&                 /* connection still used */
      ! lockPending && ! locked;       /* no other operation in progress */
      lockPending = true; requestor = nearLoc;
      reconfigured = true;
      otherAnchor = rightEnd;                       /*CHOOSE RIGHT ANCHOR*/
      newFarLoc = rightEnd;                         /*CHOOSE NEW NEIGHBOR*/
      newNearLoc = nearLoc;               /*IF MOBILE, CHOOSE NEW ADDRESS*/
      addr[farLoc]!requestLock(UDP,nearLoc,nearLoc,otherAnchor,null);
      requesting[nearLoc] = true; goto Anchoring
   :: ! reconfigured && nearLoc == null &&    /*CHOOSE RIGHT END MOBILITY*/
      (sending || receiving) &&                 /* connection still used */
      ! lockPending && ! locked;       /* no other operation in progress */
                       /*CHOOSE RIGHT ANCHOR, NEW NEIGHBOR OF LEFT ANCHOR*/
      addr[farLoc]!rightRequest(UDP,nearLoc,nearLoc,mobileAddr,null);
      reconfigured = true
   :: addr[nearLoc]?rightRequest(UDP,eval(farLoc),newSeq,newAck,aux);
      if
      :: ! reconfigured &&                     /*LEFT ANCHOR MUST BE SELF*/
         (sending || receiving) &&              /* connection still used */
         ! lockPending && ! locked;    /* no other operation in progress */
         lockPending = true; requestor = nearLoc;
         reconfigured = true;
         otherAnchor = newSeq;  
         newFarLoc = newAck;  
         newNearLoc = nearLoc;
         addr[farLoc]!requestLock(UDP,nearLoc,nearLoc,otherAnchor,null);
         requesting[nearLoc] = true; goto Anchoring
      :: else
      fi
   :: addr[nearLoc]?requestLock(UDP,eval(farLoc),newSeq,newAck,aux);
      assert (nearLoc == rightEnd);
      if
      :: newAck == nearLoc;
         if
         :: ! sending && ! receiving    /* session soon over, do nothing */
         :: sending || receiving;
            otherAnchor = newSeq; newDelta = aux;
            newFarLoc = leftEnd;                    /*CHOOSE NEW NEIGHBOR*/
            newNearLoc = nearLoc;         /*IF MOBILE, CHOOSE NEW ADDRESS*/
            addr[farLoc]!ackLock(UDP,nearLoc,newSeq,newAck,null);
            goto Anchoring
         fi;
      :: newAck != nearLoc; 
         if                                          /* anchor not found */
         :: addr[farLoc]!nackLock(UDP,nearLoc,newSeq,newAck,null)
         :: addr[farLoc]!lockTimeout(UDP,nearLoc,newSeq,newAck,null)
         fi                                              /* message lost */
      fi
   od;
endOnePath:  /* NORMAL DATA TRANSMISSION FOR MIDDLEBOXES */
   do
   /* RESPOND TO RECEIVED MESSAGES */
   :: addr[nearLoc]?dataAck(TCP,eval(farLoc),newSeq,newAck,null);
      newAck = newAck-delta; ReceiveData(); ReceiveAck();
      forward[pair[nearLoc]]!dataAck(TCP,null,newSeq,newAck,null)
   :: addr[nearLoc]?finAck(TCP,eval(farLoc),newSeq,newAck,null);
      newAck = newAck-delta;
      ReceiveFin(); ReceiveData(); ReceiveAck();
      forward[pair[nearLoc]]!finAck(TCP,null,newSeq,newAck,null)
      CheckEnding()
   :: addr[nearLoc]?ack(TCP,eval(farLoc),newSeq,newAck,null);
      newAck = newAck-delta; ReceiveAck();
      forward[pair[nearLoc]]!ack(TCP,null,null,newAck,null);
      CheckEnding()
   /* RESPOND TO FORWARDED MESSAGES (MIDDLEBOXES ONLY) */
   :: forward[nearLoc]?dataAck(TCP,null,newSeq,newAck,null);
      addr[farLoc]!dataAck(TCP,nearLoc,newSeq+delta,newAck,null);
      SendData(); SendAck()
   :: forward[nearLoc]?finAck(TCP,null,newSeq,newAck,null);
      addr[farLoc]!finAck(TCP,nearLoc,newSeq+delta,newAck,null);
      SendFin(); SendData(); SendAck()
   :: forward[nearLoc]?ack(TCP,null,null,newAck,null);
      addr[farLoc]!ack(TCP,nearLoc,null,newAck,null); 
      SendAck(); CheckEnding()
   /* LOCKING PROTOCOL FOR RECONFIGURATION, MIDDLEBOX COPY */
   :: ! reconfigured && nearLoc == null &&           /*CHOOSE LEFT ANCHOR*/
      (sending || receiving) &&                 /* connection still used */
      ! lockPending && ! locked;       /* no other operation in progress */
      lockPending = true; requestor = nearLoc;
      reconfigured = true;
      otherAnchor = rightEnd;                       /*CHOOSE RIGHT ANCHOR*/
      newFarLoc = leftExtra;                        /*CHOOSE NEW NEIGHBOR*/
      newNearLoc = nearLoc;               /*IF MOBILE, CHOOSE NEW ADDRESS*/
      addr[farLoc]!requestLock(UDP,nearLoc,nearLoc,otherAnchor,null);
      requesting[nearLoc] = true; goto Anchoring
   :: ! reconfigured && nearLoc == null &&         /*CHOOSE SELF-DELETION*/
      (sending || receiving) &&                 /* connection still used */
      ! lockPending && ! locked;       /* no other operation in progress */
                       /*CHOOSE RIGHT ANCHOR, NEW NEIGHBOR OF LEFT ANCHOR*/
      addr[farLoc]!rightRequest(UDP,nearLoc,rightEnd,rightEnd,null);
      reconfigured = true
   :: addr[nearLoc]?rightRequest(UDP,eval(farLoc),newSeq,newAck,aux);
      if
      :: ! reconfigured &&                     /*LEFT ANCHOR MUST BE SELF*/
         (sending || receiving) &&              /* connection still used */
         ! lockPending && ! locked;    /* no other operation in progress */
         lockPending = true; requestor = nearLoc;
         reconfigured = true;
         otherAnchor = newSeq;
         newFarLoc = newAck;
         newNearLoc = nearLoc;
         addr[farLoc]!requestLock(UDP,nearLoc,nearLoc,otherAnchor,null);
         requesting[nearLoc] = true; goto Anchoring
      :: else
      fi
   :: addr[nearLoc]?requestLock(UDP,eval(farLoc),newSeq,newAck,aux);
      if
      :: newAck == nearLoc;
         if
         :: ! sending && ! receiving    /* session soon over, do nothing */
         :: sending || receiving;
            otherAnchor = newSeq; newDelta = aux;
            newFarLoc = mobileAddr;                 /*CHOOSE NEW NEIGHBOR*/
            newNearLoc = nearLoc;         /*IF MOBILE, CHOOSE NEW ADDRESS*/
            addr[farLoc]!ackLock(UDP,nearLoc,newSeq,newAck,null);
            goto Anchoring
         fi;
      :: newAck != nearLoc;                         /* normal forwarding */
            forward[pair[nearLoc]]!requestLock(UDP,null,newSeq,newAck,aux)
      :: newAck != nearLoc;                              /* message lost */
            addr[farLoc]!lockTimeout(UDP,nearLoc,null,null,null)
      fi
   :: forward[nearLoc]?requestLock(UDP,null,newSeq,newAck,aux);
      assert (! lockPending || newSeq == requestor);
                          /* latter is case of retransmitted requestLock */
      addr[farLoc]!requestLock(UDP,nearLoc,newSeq,newAck,aux+delta); 
      lockPending = true; requestor = newSeq
   :: addr[nearLoc]?ackLock(UDP,eval(farLoc),newSeq,newAck,aux);
      assert ((locked || lockPending) && newSeq == requestor);
      if
      :: lockPending = false; locked = true;        /* normal forwarding */
         forward[pair[nearLoc]]!ackLock(UDP,null,newSeq,newAck,aux)
      ::                                                 /* message lost */
         forward[pair[nearLoc]]!lockTimeout(UDP,null,null,null,null)
      fi
   :: forward[nearLoc]?ackLock(UDP,null,newSeq,newAck,aux);
      addr[farLoc]!ackLock(UDP,nearLoc,newSeq,newAck,aux+delta)
   :: addr[nearLoc]?nackLock(UDP,eval(farLoc),newSeq,null,null);
      assert ((locked || lockPending) && newSeq == requestor);
      lockPending = false; locked = false; requestor = null
      forward[pair[nearLoc]]!nackLock(UDP,null,newSeq,null,null)
   :: forward[nearLoc]?nackLock(UDP,null,newSeq,null,null);
      addr[farLoc]!nackLock(UDP,nearLoc,newSeq,null,null)
   :: addr[nearLoc]?lockTimeout(UDP,eval(farLoc),null,null,null);
      forward[pair[nearLoc]]!lockTimeout(UDP,null,null,null,null)
   :: forward[nearLoc]?lockTimeout(UDP,null,null,null,null);
      addr[farLoc]!lockTimeout(UDP,nearLoc,null,null,null)
   :: addr[nearLoc]?cancelLock(UDP,eval(farLoc),newSeq,null,null);
      forward[pair[nearLoc]]!cancelLock(UDP,null,newSeq,null,null)
   :: forward[nearLoc]?cancelLock(UDP,null,newSeq,null,null);
      assert(locked && newSeq == requestor); 
      lockPending = false; locked = false; requestor = null;
      addr[farLoc]!cancelLock(UDP,nearLoc,newSeq,null,null)
   :: addr[nearLoc]?ackCancel(UDP,eval(farLoc),newSeq,null,null);
      forward[pair[nearLoc]]!ackCancel(UDP,null,newSeq,null,null)
   :: forward[nearLoc]?ackCancel(UDP,null,newSeq,null,null);
      addr[farLoc]!ackCancel(UDP,nearLoc,newSeq,null,null)
   od;
Anchoring:
   do
/* any left-anchor substate */
   :: forward[nearLoc]?requestLock(UDP,null,newSeq,newAck,aux);
      if 
      :: requesting[nearLoc];
         requestPendingRequestor = newSeq; requestPendingAnchor = newAck;
         requestPendingDelta = aux
      :: else; forward[pair[nearLoc]]!nackLock(UDP,null,newSeq,null,null)
      fi
/* Requesting substate */
   :: addr[nearLoc]?ackLock(UDP,eval(farLoc),newSeq,newAck,aux);
      assert ((locked||lockPending) && newAck==otherAnchor);
      if
      :: ! sending && ! receiving       /* session soon over, do nothing */
      :: sending || receiving;
         if
         :: requestPendingAnchor != null;
            forward[pair[nearLoc]]!
               nackLock(UDP,null,requestPendingRequestor,null,null);
            requestPendingRequestor = null; requestPendingAnchor = null;
            requestPendingDelta = null
         :: else
         fi;
         locked = true; newDelta = aux; requestor = null;
         addr[newFarLoc]!syn(UDP,newNearLoc,null,null,null);
         requesting[nearLoc] = false
      fi
   :: addr[nearLoc]?nackLock(UDP,eval(farLoc),newSeq,null,null);
      assert ((locked || lockPending) && newSeq == nearLoc);
      newFarLoc = null; otherAnchor = null;
      if
      :: requestPendingRequestor != null;
         addr[farLoc]!requestLock(UDP,nearLoc,
            requestPendingRequestor,requestPendingAnchor,
                                                requestPendingDelta+delta);
         requestor = requestPendingRequestor;
         requestPendingRequestor = null
      :: else; requestor = null; lockPending = false
      fi;
      requesting[nearLoc] = false; 
      if :: behavior == endpoint; goto OnePath :: else; goto endOnePath fi;
   :: addr[nearLoc]?lockTimeout(UDP,eval(farLoc),null,null,null);
      if
      :: sending || receiving;                      /* connection in use */
         addr[farLoc]!requestLock(UDP,nearLoc,nearLoc,otherAnchor,null)
      :: ! (sending || receiving);      /* session soon over, do nothing */
         requesting[nearLoc] = false; 
         if :: behavior == endpoint; goto OnePath 
            :: else; goto endOnePath fi
      fi
/* Requested substate */
   :: addr[nearLoc]?requestLock(UDP,eval(farLoc),newSeq,newAck,aux);
                                            /* retransmitted requestLock */
      assert (newSeq==otherAnchor && newAck==nearLoc && aux==newDelta);
      addr[farLoc]!ackLock(UDP,nearLoc,newSeq,newAck,null)
   :: addr[newNearLoc]?syn(UDP,eval(newFarLoc),newSeq,newAck,aux);
      if
      :: ! sending && ! receiving       /* session soon over, do nothing */
      :: else;
         addr[newFarLoc]!synAck(UDP,newNearLoc,null,null,null)
      fi
/* Requested or Waiting substate */
   :: addr[nearLoc]?cancelLock(UDP,eval(farLoc),newSeq,null,null);
      assert (newSeq == otherAnchor);
      addr[farLoc]!ackCancel(UDP,nearLoc,newSeq,null,null);
      if :: behavior == endpoint; goto OnePath :: else; goto endOnePath fi
/* SettingUp substate */
   :: addr[newNearLoc]?synAck(UDP,eval(newFarLoc),newSeq,newAck,aux);
      if
      :: ! sending && ! receiving       /* session soon over, do nothing */
      :: else;
         addr[newFarLoc]!ack(UDP,newNearLoc,null,null,null);
         oldSent = sent; oldSentAcked = sentAcked; 
         oldRcvd = rcvd; oldRcvdAcked = rcvdAcked;
         sentAcked = sent; firstNewRcvd = null; 
         newDelta = newDelta + delta; goto TwoPaths
      fi
   :: addr[newNearLoc]?synTimeout(UDP,eval(newFarLoc),newSeq,newAck,aux);
      addr[farLoc]!cancelLock(UDP,nearLoc,nearLoc,null,null)
/* Waiting substate */
   :: addr[newNearLoc]?ack(UDP,eval(newFarLoc),newSeq,newAck,aux);
      oldSent = sent; oldSentAcked = sentAcked; 
      oldRcvd = rcvd; oldRcvdAcked = rcvdAcked;
      sentAcked = sent; firstNewRcvd = null; newDelta = newDelta + delta;
      goto TwoPaths
/* Canceling substate */
   :: addr[nearLoc]?ackCancel(UDP,eval(farLoc),newSeq,null,null);
      assert (newSeq == nearLoc); 
      if :: behavior == endpoint; goto OnePath :: else; goto endOnePath fi
   /* RESPOND TO RECEIVED AND FORWARDED DATA */
   :: addr[nearLoc]?dataAck(TCP,eval(farLoc),newSeq,newAck,null);
      newAck = newAck-delta; ReceiveData(); ReceiveAck();
      if
      :: behavior == endpoint;
         addr[farLoc]!ack(TCP,nearLoc,null,rcvd,null); SendAck()
      :: else; forward[pair[nearLoc]]!dataAck(TCP,null,newSeq,newAck,null)
      fi
   :: forward[nearLoc]?dataAck(TCP,null,newSeq,newAck,null);
      addr[farLoc]!dataAck(TCP,nearLoc,newSeq+delta,newAck,null);
      SendData(); SendAck()
   /* RESPOND TO RECEIVED AND FORWARDED FINS */
   :: addr[nearLoc]?finAck(TCP,eval(farLoc),newSeq,newAck,null);
      newAck = newAck-delta;
      ReceiveFin(); ReceiveData(); ReceiveAck();
      if
      :: behavior == endpoint;
         if
         :: sending;                          /* this endpoint ends also */
            addr[farLoc]!finAck(TCP,nearLoc,sent+delta,rcvd,null);
            SendFin(); SendData(); SendAck()
         :: sending || ! sending;              /* endpoint is not ending */
            addr[farLoc]!ack(TCP,nearLoc,null,rcvd,null); SendAck()
         fi;
      :: else; forward[pair[nearLoc]]!finAck(TCP,null,newSeq,newAck,null)
      fi;
      CheckEnding()
   :: addr[nearLoc]?ack(TCP,eval(farLoc),null,newAck,null);
      newAck = newAck-delta; ReceiveAck();
      if
      :: behavior == endpoint
      :: else; forward[pair[nearLoc]]!ack(TCP,null,null,newAck,null)
      fi;
      CheckEnding()
   :: forward[nearLoc]?finAck(TCP,null,newSeq,newAck,null);
      addr[farLoc]!finAck(TCP,nearLoc,newSeq+delta,newAck,null);
      SendFin(); SendData(); SendAck()
   :: forward[nearLoc]?ack(TCP,null,null,newAck,null);
      addr[farLoc]!ack(TCP,nearLoc,null,newAck,null); SendAck()
      CheckEnding()
   od;
TwoPaths: /* state for both anchors */
   CheckCompletion();
   do
   :: forward[nearLoc]?requestLock(UDP,null,newSeq,newAck,aux);
      forward[pair[nearLoc]]!nackLock(UDP,null,newSeq,null,null)
   /* DATA */
   :: (nearLoc == leftEnd || nearLoc == mobileAddr) &&
      sending && (sent < dataLimit+100-1);
      addr[newFarLoc]!dataAck(TCP,newNearLoc,sent+newDelta,rcvd,null);
      SendData(); SendNewAck()
   :: (nearLoc == rightEnd || nearLoc == mobileAddr) &&
      sending && (sent < dataLimit+600);
      addr[newFarLoc]!dataAck(TCP,newNearLoc,sent+newDelta,rcvd,null);
      SendData(); SendNewAck()
   :: addr[nearLoc]?dataAck(TCP,eval(farLoc),newSeq,newAck,null);
      newAck = newAck-delta; ReceiveOldData(); ReceiveOldAck();
      if
      :: behavior == endpoint;
         addr[farLoc]!ack(TCP,nearLoc,null,oldRcvd,null); 
         SendOldAck(oldRcvd)
      :: else; forward[pair[nearLoc]]!dataAck(TCP,null,newSeq,newAck,null)
      fi;
      CheckCompletion()
   :: addr[newNearLoc]?dataAck(TCP,eval(newFarLoc),newSeq,newAck,null);
      assert (newSeq >= oldRcvd);
      newAck = newAck-newDelta; ReceiveNewData(); ReceiveNewAck();
      if
      :: behavior == endpoint;
         addr[newFarLoc]!ack(TCP,newNearLoc,null,rcvd,null); SendNewAck()
      :: else; forward[pair[nearLoc]]!dataAck(TCP,null,newSeq,newAck,null)
      fi
   :: forward[nearLoc]?dataAck(TCP,null,newSeq,newAck,null);
      if
      :: newSeq < oldSent && newAck <= oldRcvd; 
         addr[farLoc]!dataAck(TCP,nearLoc,newSeq+delta,newAck,null);
         SendOldAck(newAck)
      :: newSeq < oldSent && newAck > oldRcvd; 
         addr[farLoc]!dataAck(TCP,nearLoc,newSeq+delta,oldRcvdAcked,null);
         addr[newFarLoc]!ack(TCP,newNearLoc,null,newAck,null);
         SendNewAck()
      :: newSeq >= oldSent && newAck <= oldRcvd && newAck > oldRcvdAcked;
         addr[farLoc]!ack(TCP,nearLoc,null,newAck,null);
         SendOldAck(newAck);
         addr[newFarLoc]!
                    dataAck(TCP,newNearLoc,newSeq+newDelta,rcvdAcked,null);
         SendData()
      :: newSeq >= oldSent && (newAck > oldRcvd || newAck <= oldRcvdAcked);
         addr[newFarLoc]!
                       dataAck(TCP,newNearLoc,newSeq+newDelta,newAck,null);
         SendData(); SendNewAck()
      fi;
      CheckCompletion()
   /* FINS AND ACKS ON OLD PATH */
   :: addr[nearLoc]?finAck(TCP,eval(farLoc),newSeq,newAck,null);
      newAck = newAck-delta;
      ReceiveFin(); ReceiveOldData(); ReceiveOldAck();
      if
      :: behavior == endpoint;
         addr[farLoc]!ack(TCP,nearLoc,null,oldRcvd,null); 
         SendOldAck(oldRcvd);
         if
         :: sending;                          /* this endpoint ends also */
            addr[newFarLoc]!finAck(TCP,newNearLoc,sent+newDelta,rcvd,null);
            SendFin(); SendData(); SendNewAck()
         :: sending || ! sending        /* endpoint does not want to end */
         fi;
      :: else; forward[pair[nearLoc]]!finAck(TCP,null,newSeq,newAck,null)
      fi;
      CheckCompletion()
   :: addr[nearLoc]?ack(TCP,eval(farLoc),null,newAck,null);
      newAck = newAck-delta; ReceiveOldAck();
      if
      :: behavior == endpoint
      :: else; forward[pair[nearLoc]]!ack(TCP,null,null,newAck,null)
      fi;
      CheckCompletion()
   /* FINS AND ACKS ON NEW PATH */
   :: behavior == endpoint && sending;
      addr[newFarLoc]!finAck(TCP,newNearLoc,sent+newDelta,rcvd,null);
      SendFin(); SendData(); SendNewAck()
   :: addr[newNearLoc]?finAck(TCP,eval(newFarLoc),newSeq,newAck,null);
      newAck = newAck-newDelta;
      ReceiveFin(); ReceiveNewData(); ReceiveNewAck();
      if
      :: behavior == endpoint;
         addr[newFarLoc]!ack(TCP,newNearLoc,null,rcvd,null); SendNewAck();
         if
         :: sending;                          /* this endpoint ends also */
            addr[newFarLoc]!finAck(TCP,newNearLoc,sent+newDelta,rcvd,null);
            SendFin(); SendData(); SendNewAck()
         :: sending || ! sending        /* endpoint does not want to end */
         fi;
      :: else; forward[pair[nearLoc]]!finAck(TCP,null,newSeq,newAck,null)
      fi;
      CheckCompletion()
   :: addr[newNearLoc]?ack(TCP,eval(newFarLoc),null,newAck,null);
      newAck = newAck-newDelta; ReceiveNewAck();
      if
      :: behavior == endpoint
      :: else; forward[pair[nearLoc]]!ack(TCP,null,null,newAck,null)
      fi;
      CheckCompletion()
   :: forward[nearLoc]?finAck(TCP,null,newSeq,newAck,null);
      if
      :: newSeq < oldSent && newAck <= oldRcvd; 
         addr[farLoc]!finAck(TCP,nearLoc,newSeq+delta,newAck,null);
         SendFin(); SendOldAck(newAck)
      :: newSeq < oldSent && newAck > oldRcvd; 
         addr[farLoc]!finAck(TCP,nearLoc,newSeq+delta,oldRcvdAcked,null);
         SendFin();
         addr[newFarLoc]!ack(TCP,newNearLoc,null,newAck,null);
         SendNewAck()
      :: newSeq >= oldSent && newAck <= oldRcvd && newAck > oldRcvdAcked;
         addr[farLoc]!ack(TCP,nearLoc,null,newAck,null);
         SendOldAck(newAck);
         addr[newFarLoc]!
                     finAck(TCP,newNearLoc,newSeq+newDelta,rcvdAcked,null);
         SendFin(); SendData(); SendNewAck()
      :: newSeq >= oldSent && (newAck > oldRcvd || newAck <= oldRcvdAcked);
         addr[newFarLoc]!
                        finAck(TCP,newNearLoc,newSeq+newDelta,newAck,null);
         SendFin(); SendData(); SendNewAck()
      fi; 
      CheckCompletion()
   :: forward[nearLoc]?ack(TCP,null,null,newAck,null);
      if   
      :: newAck <= oldRcvd && newAck > oldRcvdAcked;              
         addr[farLoc]!ack(TCP,nearLoc,null,newAck,null); 
         SendOldAck(newAck)
      :: newAck <= oldRcvd && newAck <= oldRcvdAcked
      :: newAck > oldRcvd;               
         addr[newFarLoc]!ack(TCP,newNearLoc,null,newAck,null); SendNewAck()
      fi; 
      CheckCompletion()
   od;
end:   if :: checkpoint; goto Checkpoint :: else; goto endAgain fi;
Checkpoint:
   if                         /* cannot happen, so process is stuck here */
   :: addr[nearLoc]?syn(TCP,eval(farLoc),newSeq,newAck); goto endAgain
   fi;
endAgain: skip
}

init { atomic {
                run portProcess(leftEnd);
                run portProcess(leftMbox);
                run portProcess(rightMbox);
                run portProcess(leftExtra);
                run portProcess(rightExtra);
                run portProcess(rightEnd);
      }       }

/* ------------------------------------------------------------------------
SAMPLE SPIN RUNS

INITIAL CONFIGURATION NO MIDDLEBOXES
insert two middleboxes
   dataLimit 3, 2507 MB, depth 545, search complete

INITIAL CONFIGURATION ONE MIDDLEBOX MBOX
delete Mbox
   dataLimit 3, 16737 MB, depth 408, search complete 
insert Extra to right of Mbox
   dataLimit 3, 12478 MB, depth 553, search complete
replace Mbox with Extra
   dataLimit 3, 4308 MB, depth 438, search complete

INITIAL CONFIGURATION TWO MIDDLEBOXES
delete two middleboxes
   dataLimit 3, 36388 MB, depth 552, search complete

HOW TO RUN SPIN ON A 64GB UBUNTU VM

1 load VM
  sftp: dysco3.pml 
  sftp: spin645.tar
  ssh
  $ tar -xf *.tar
  $ cp dysco3.pml Spin/Src*/.
  $ cd Spin/Src*
  $ sudo apt-get install make byacc flex
  $ make

2 compile Spin
  $ ./spin -a dysco3.pml
  $gcc -DVECTORSZ=2048 -DSAFETY -DCOLLAPSE -m64 -DMEMLIM=65536 -o pan pan.c

3 run Spin
  $ nohup ./pan &
------------------------------------------------------------------------ */
