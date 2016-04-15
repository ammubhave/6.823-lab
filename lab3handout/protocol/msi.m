-- 6.823 Lab 3
-- Protocol Framework

-- See the lines marked TODO in file below.
-- NOTE: this file will not compile until you fill in some minimal
-- additional states and messages.  Try adding support for just a 
-- read request first (comment out other starting rules).

----------------------------------------------------------------------
-- Constants
----------------------------------------------------------------------
const
  -- TODO: start with this number at 1, then increase to 2 and eventually 3
  ProcCount: 1;          -- number processors

  VC0: 0;                -- low priority
  VC1: 1;
  VC2: 2;                -- high priority
  NumVCs: VC2 - VC0 + 1;
  QMax: 2;
  NetMax: ProcCount+1;
  

----------------------------------------------------------------------
-- Types
----------------------------------------------------------------------
type
  Proc: scalarset(ProcCount);   -- unordered range of processors
  Home: enum { HomeType };      -- need enumeration for IsMember calls
  Node: union { Home , Proc };

  VCType: VC0..NumVCs-1;

  -- Message enumeration: you must support the first three, but will need to
  -- add more message types (e.g., various ACKs)
  MessageType: enum {  ReadReq,         -- request for shared state
                       ReadAck,
                       WriteReq,        -- write request
                       WriteAck,
                       WBResp,            -- writeback request (w/ or wo/ data)
                       RecallReq
                    };

  Message:
    Record
      mtype: MessageType;
      src: Node;
      -- don't need a destination for verification
      vc: VCType;
      aux: Node;  -- participating node (used when forwarding msgs)
      cnt: 0..ProcCount;
    End;

  HomeState:
    Record
      -- home node state: you have three stable states (M,S,I) but need to
      -- add transient states during races
      state: enum { HM, HS, HI,
                    HMTM_Pending, HMTS_Pending, HSTM_Pending };

      owner: Node;  
      sharers: multiset [ProcCount] of Node;
    End;

  ProcState:
    Record
      -- processor state: again, three stable states (M,S,I) but you need to
      -- add transient states to support races
      state: enum { PM, PS, PI,
                    PTS_Pending, PTM_Pending};
    End;

----------------------------------------------------------------------
-- Variables
----------------------------------------------------------------------
var
  HomeNode:  HomeState;
  Procs: array [Proc] of ProcState;
  Net:   array [Node] of multiset [NetMax] of Message;
  msg_processed: boolean;

----------------------------------------------------------------------
-- Procedures
----------------------------------------------------------------------
Procedure Send(mtype:MessageType;
               dst:Node;
               src:Node;
               vc:VCType;
               aux:Node;
               cnt:0..ProcCount);
var msg:Message;
Begin
  Assert (MultiSetCount(i:Net[dst], true) < NetMax) "Too many messages";
  msg.mtype := mtype;
  msg.src   := src;
  msg.vc    := vc;
  msg.aux   := aux;
  msg.cnt   := cnt;
  MultiSetAdd(msg, Net[dst]);
End;

Procedure ErrorUnhandledMsg(msg:Message; n:Node);
Begin
  /*switch msg.mtype
  case WriteReq, ReadReq, WBReq:
    msg_processed := false;  -- we can receive a raw request any time
  else*/
    error "Unhandled message type!";
  --endswitch;
End;

Procedure ErrorUnhandledState();
Begin
  error "Unhandled state!";
End;

Function NumberOfSharers(): 0..ProcCount;
Begin
  return MultiSetCount(i:HomeNode.sharers, true);
End;

Function IsSharer(n:Node) : boolean;
Begin
  if MultiSetCount(i:HomeNode.sharers, HomeNode.sharers[i] = n) > 0
  then
    return true;
  else
    return false;
  endif;
End;

Procedure AddToSharersList(n:Node);
Begin
  if MultiSetCount(i:HomeNode.sharers, HomeNode.sharers[i] = n) = 0
  then
    MultiSetAdd(n, HomeNode.sharers);
  endif;
End;

Procedure RemoveFromSharersList(n:Node);
Begin
  MultiSetRemovePred(i:HomeNode.sharers, HomeNode.sharers[i] = n);
End;

Procedure HomeReceive(msg:Message);
var cnt:0..ProcCount;
Begin
  -- compiler barfs if we put this inside the switch
  cnt := MultiSetCount(i:HomeNode.sharers, true);

  -- default to 'processing' message.  set to false otherwise
  msg_processed := true;

  switch HomeNode.state
  case HI:
    switch msg.mtype

    case ReadReq:
      HomeNode.state := HS;
      AddToSharersList(msg.src);
      Send(ReadAck, msg.src, HomeType, VC1, msg.src, cnt);

    case WriteReq:
      HomeNode.state := HM;
      HomeNode.owner := msg.src;
      Send(WriteAck, msg.src, HomeType, VC1, msg.src, cnt);

    else
      ErrorUnhandledMsg(msg, HomeType);

    endswitch;

  case HM:
    switch msg.mtype

    case ReadReq:
      HomeNode.state := HMTS_Pending;
      AddToSharersList(msg.src);
      Send(RecallReq, HomeNode.owner, HomeType, VC0, HomeNode.owner, cnt);
      undefine HomeNode.owner
      
    case WriteReq:
      HomeNode.state := HMTM_Pending;
      Send(RecallReq, HomeNode.owner, HomeType, VC0, HomeNode.owner, cnt);
      HomeNode.owner := msg.src;
      
    case WBResp:
      assert (msg.src = HomeNode.owner) "Writeback from non-owner";
      HomeNode.state := HI;
      undefine HomeNode.owner

    else
      ErrorUnhandledMsg(msg, HomeType);

    endswitch;

  case HS:
    Assert (cnt != 0) "sharers list empty, but line is shared";

    switch msg.mtype

    case ReadReq:
      AddToSharersList(msg.src);
      Send(ReadAck, msg.src, HomeType, VC1, msg.src, cnt);

    case WriteReq:
      RemoveFromSharersList(msg.src);
      cnt := NumberOfSharers();
      if cnt > 0
      then
        HomeNode.state := HSTM_Pending;
        for n:Proc do
          if IsSharer(n)
          then
            Send(RecallReq, n, HomeType, VC0, n, cnt);
          endif;
        endfor;
      else
        HomeNode.state := HM;
        Send(WriteAck, msg.src, HomeType, VC1, HomeNode.owner, cnt);
      endif;
      HomeNode.owner := msg.src;

    case WBResp:
      RemoveFromSharersList(msg.src);
      cnt := NumberOfSharers();      
      if cnt = 0
      then
        HomeNode.state := HI;
      endif;
      
    else
      ErrorUnhandledMsg(msg, HomeType);

    endswitch;

  case HMTM_Pending:
    switch msg.mtype

    case WBResp:
      HomeNode.state := HM;
      Send(WriteAck, HomeNode.owner, HomeType, VC1, HomeNode.owner, cnt);

    case ReadReq:
      msg_processed := false;

    case WriteReq:
      msg_processed := false;

    else
      ErrorUnhandledMsg(msg, HomeType);

    endswitch;

  case HMTS_Pending:
    switch msg.mtype

    case WBResp:
      HomeNode.state := HS;
      for n:Proc do
        if IsSharer(n)
        then
          Send(ReadAck, n, HomeType, VC1, n, cnt);
        endif;
      endfor;

    case ReadReq:
      msg_processed := false;

    case WriteReq:
      msg_processed := false;

    else
      ErrorUnhandledMsg(msg, HomeType);

    endswitch;

  case HSTM_Pending:
    switch msg.mtype

    case WBResp:
      RemoveFromSharersList(msg.src);
      cnt := NumberOfSharers();
      if cnt = 0 then
        HomeNode.state := HM;
        Send(WriteAck, HomeNode.owner, HomeType, VC1, HomeNode.owner, cnt);
      endif;

    case ReadReq:
      msg_processed := false;

    case WriteReq:
      msg_processed := false;

    else
      ErrorUnhandledMsg(msg, HomeType);

    endswitch;

  -- TODO: add other cases from home node state here!

  endswitch;

End;

Procedure ProcReceive(msg:Message; p:Proc);
Begin
  -- default to 'processing' message.  set to false otherwise
  msg_processed := true;

  alias ps:Procs[p].state do

  switch ps
  case PI:

    switch msg.mtype
    case RecallReq:
      Send(WBResp, msg.src, p, VC1, msg.src, 0);
    else
      ErrorUnhandledMsg(msg, p);
    endswitch;

  case PM:

    switch msg.mtype
    case RecallReq:
      Send(WBResp, msg.src, p, VC1, msg.src, 0);
      ps := PI;
    else
      ErrorUnhandledMsg(msg, p);
    endswitch;

  case PS:

    switch msg.mtype
    case RecallReq:
      Send(WBResp, msg.src, p, VC1, msg.src, 0);
      ps := PI;
    else
      ErrorUnhandledMsg(msg, p);
    endswitch;

  case PTS_Pending:

    switch msg.mtype
    case ReadAck:
      ps := PS;
    case RecallReq:
      msg_processed := false; -- stall message in InBox
    else
      ErrorUnhandledMsg(msg, p);
    endswitch;

  case PTM_Pending:

    switch msg.mtype
    case WriteAck:
      ps := PM;
    case RecallReq:
      msg_processed := false; -- stall message in InBox
    else
      ErrorUnhandledMsg(msg, p);
    endswitch;

  ----------------------------
  -- Error catch
  ----------------------------
  else
    ErrorUnhandledState();

  endswitch;

  endalias;
End;

----------------------------------------------------------------------
-- Rules
----------------------------------------------------------------------

-- Processor actions (affecting coherency)
ruleset n:Proc Do
  alias p:Procs[n] Do

  rule "read request"
    p.state = PI
  ==>
    Send(ReadReq, HomeType, n, VC0, UNDEFINED, UNDEFINED);
    p.state := PTS_Pending;
  endrule;

  rule "write request"
    (p.state = PI)
  ==>
    Send(WriteReq, HomeType, n, VC0, UNDEFINED, UNDEFINED);
    p.state := PTM_Pending;
  endrule;

  rule "upgrade request"
    (p.state = PS)
  ==>
    Send(WriteReq, HomeType, n, VC0, UNDEFINED, UNDEFINED);
    p.state := PTM_Pending;
  endrule;

  rule "writeback"
    (p.state = PM)
  ==>
    Send(WBResp, HomeType, n, VC2, UNDEFINED, UNDEFINED);
    p.state := PI;
  endrule;

  rule "evict"
    (p.state = PS)
  ==>
    Send(WBResp, HomeType, n, VC2, UNDEFINED, UNDEFINED);
    p.state := PI;
  endrule;

  endalias;
endruleset;

-- receive rules
ruleset n:Node do
  choose midx:Net[n] do
    alias chan:Net[n] do
    alias msg:chan[midx] do

    rule "receive"
      (msg.vc = VC2) |
      (msg.vc = VC1 & MultiSetCount(m:chan, chan[m].vc = VC2)  = 0) |
      (msg.vc = VC0 & MultiSetCount(m:chan, chan[m].vc = VC2)  = 0 
                    & MultiSetCount(m:chan, chan[m].vc = VC1)  = 0)
    ==>

      if IsMember(n, Home)
      then
        HomeReceive(msg);

        if msg_processed
        then
          MultiSetRemove(midx, chan);
        endif;

      else
        ProcReceive(msg, n);

        if msg_processed
        then
          MultiSetRemove(midx, chan);
        endif;
    
      endif;

    endrule;
  
    endalias;
    endalias;
  endchoose;  
endruleset;

----------------------------------------------------------------------
-- Startstate
----------------------------------------------------------------------
startstate

  -- home node initialization
  HomeNode.state := HI;
  undefine HomeNode.sharers;
  undefine HomeNode.owner;
  --undefine HomeNode.pending_node;
  
  -- processor initialization
  for i:Proc do
    Procs[i].state := PI;
    -- TODO: any other initialization?
  endfor;

  -- network initialization
  undefine Net;
endstartstate;

----------------------------------------------------------------------
-- Invariants
----------------------------------------------------------------------

invariant "modified w/ empty sharers list"
  HomeNode.state = HM
    ->
      MultiSetCount(i:HomeNode.sharers, true) = 0;

invariant "modified to modified pending w/ empty sharers list"
  HomeNode.state = HMTM_Pending
    ->
      MultiSetCount(i:HomeNode.sharers, true) = 0;

invariant "modified to modified pending w/ owner"
  HomeNode.state = HMTM_Pending
    ->
      IsUndefined(HomeNode.owner) = false;

invariant "modified to shared pending w/o owner"
  HomeNode.state = HMTS_Pending
    ->
      IsUndefined(HomeNode.owner) = true;

invariant "modified w/ owner"
  HomeNode.state = HM
    ->
      IsUndefined(HomeNode.owner) = false;

invariant "shared w/o empty sharers list"
  HomeNode.state = HS
    ->
      MultiSetCount(i:HomeNode.sharers, true) > 0;

invariant "shared w/o owner"
  HomeNode.state = HS
    ->
      IsUndefined(HomeNode.owner) = true;

invariant "invalid w/ empty sharers list"
  HomeNode.state = HI
    ->
      MultiSetCount(i:HomeNode.sharers, true) = 0;

invariant "invalid w/o owner"
  HomeNode.state = HI
    ->
      IsUndefined(HomeNode.owner) = true;
/*
invariant "shared or modified home with shared proc"
  HomeNode.state = HI
    ->
      MultiSetCount(i:Procs, Procs[i].state = PS | Procs[i].state = PM) = 0;
*/