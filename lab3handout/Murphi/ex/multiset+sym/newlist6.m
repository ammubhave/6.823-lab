--------------------------------------------------------------------------
-- Copyright (C) 1992, 1993 by the Board of Trustees of        		  
-- Leland Stanford Junior University.					  
--									  
-- This description is provided to serve as an example of the use	  
-- of the Murphi description language and verifier, and as a benchmark	  
-- example for other verification efforts.				  
--									  
-- License to use, copy, modify, sell and/or distribute this description  
-- and its documentation any purpose is hereby granted without royalty,   
-- subject to the following terms and conditions, provided		  
--									  
-- 1.  The above copyright notice and this permission notice must	  
-- appear in all copies of this description.				  
-- 									  
-- 2.  The Murphi group at Stanford University must be acknowledged	  
-- in any publication describing work that makes use of this example. 	  
-- 									  
-- Nobody vouches for the accuracy or usefulness of this description	  
-- for any purpose.							  
--------------------------------------------------------------------------

--------------------------------------------------------------------------
--
--                                                                        
-- File:        newlist6.m                                                
--                                                                        
-- Content:     distributed list using a single linked list
--		with unordered network
--                                                                        
-- Date created:         Feb 95
--                                                                        
--------------------------------------------------------------------------

const
  CellCount: 5;
  NetMax: CellCount+3;

Type
  HeadCells: enum { HeadCell };
  RestCells: Scalarset(CellCount-1);
  CellPtr: Union{ HeadCells , RestCells};

  Cell:
    Record
      Next: CellPtr;
      State: Enum {Normal, Wait_Head, Wait_Pred, Wait_Del_Ack};
      IsInList: Boolean;  -- Does it think it's in the list?
			  -- For the invariant.
    End;    

  Message_Type:	enum{Add, Head, Delete, Pred, ChNext, Del_Ack};

  Message:
    Record
      MType: Message_Type;
      Source: CellPtr;
      Destination: CellPtr;
      Old: CellPtr;	-- old .next value	
      New: CellPtr;	-- new .next value
    End;
  


Var
  Cells: Array [CellPtr] of Cell;
  Net: MultiSet [ NetMax ] of Message;

-- Network functions

Procedure Send(t: Message_Type;
	       Dst: CellPtr;
	       Src: CellPtr;
	       New: CellPtr;
	       Old: CellPtr);
Var msg:Message;
Begin
  msg.MType := t;
  msg.Source := Src;
  msg.Destination := Dst;
  msg.New := New; 
  msg.Old := Old;  
  MultiSetAdd(msg, Net);
End;

-- Special send functions
Procedure Send_Add(d:CellPtr; s:CellPtr);
Begin
  Send(Add, d, s, UNDEFINED, UNDEFINED);
End;

Procedure Send_Head(d:CellPtr; s:CellPtr; c:CellPtr);
Begin
  Send(Head, d, s, c, UNDEFINED);
End;

Procedure Send_Pred(d:CellPtr; s:CellPtr);
Begin
  Send(Pred, d, s, UNDEFINED, UNDEFINED);
End;

Procedure Send_ChNext(d:CellPtr; s:CellPtr; old:CellPtr; new:CellPtr);
  Send(ChNext, d, s, new, old);
End;

Procedure Send_Del_Ack(d:CellPtr; s:CellPtr);
Begin
  Send(Del_Ack, d, s, UNDEFINED, UNDEFINED);
End;


--------------------
-- Rules
--------------------

  -- These rules spontaneously initiate add/delete requests
  -- from cells other than 0.
  Ruleset c:RestCells Do
    Rule
      Cells[c].State = Normal
      & !Cells[c].IsInList
      & MultiSetCount(j:Net,true) < NetMax - 3  -- plenty of room in net.
    ==>
      Send_Add(HeadCell, c);
      Cells[c].State := Wait_Head;
    End;

    Rule
      Cells[c].State = Normal
      & Cells[c].IsInList
      & MultiSetCount(j:Net,true) < NetMax - 3  -- plenty of room in net.
    ==>
      Cells[c].State := Wait_Pred;
    End;
  Endruleset;    

  Choose msg_id:Net Do
    Rule
      true
    ==>
      Alias n:Net[msg_id].Destination; me:Cells[Net[msg_id].Destination] Do

	Switch Net[msg_id].MType

        --------------------
	-- messages for home node.	
	--------------------
	Case Add:
	  -- Sent from list cell to HeadCell.
	  -- Tell cell what the old list head is.
	  If me.State = Normal
	  Then
	    Send_Head(Net[msg_id].Source, HeadCell, me.Next);
	    me.Next := Net[msg_id].Source;
	    MultiSetRemove(msg_id, Net);
	  Else
	    -- Block the message
	  Endif;

	Case Delete:
	  -- This is sent to 0 from a cell that wants
	  -- to delete itself.

	--------------------  
	-- Rules for remote cells.	
	--------------------
	Case Head:
	  -- We're adding.  Head message from 0 tells
	  -- us what our new Next should be.
	  Assert me.State = Wait_Head;
	  me.Next := Net[msg_id].New;
	  me.IsInList := true;
	  me.State := Normal;
	  MultiSetRemove(msg_id, Net);

	Case Pred:
	  Switch me.State
	  Case Normal:
	    -- Forward the message to Next and forget it.
	    Send_Pred(me.Next, n);
	    MultiSetRemove(msg_id, Net);

	  Case Wait_Pred:
	    me.State := Wait_Del_Ack;
	    Send_ChNext(Net[msg_id].Source, n, n, me.Next);
  	    MultiSetRemove(msg_id, Net);

	  Case Wait_Head:
	    -- Block all messages

	  Else
	    Error "Got Pred in funny state";
	  Endswitch;

	--------------------
	-- This may go to 0 or other.
	--------------------
	Case ChNext:
	  -- I've been told to change my next pointer.
	  If me.State = Normal | me.State = Wait_Pred
	  Then
	    If me.Next != Net[msg_id].Old 
	    Then
	      -- forward if another cell got inserted between us and 
	      -- cell to be deleted (we're 0).
	      Send_ChNext(Cells[n].Next, n, Net[msg_id].Old, Net[msg_id].New);
	    Else
	      me.Next := Net[msg_id].New;
	      Send_Del_Ack(Net[msg_id].Old, n);
	      Send_Pred(Net[msg_id].New, n);
	    Endif;
	    MultiSetRemove(msg_id, Net);
	  else  
	    -- else, block the message.
	  Endif;

	Case Del_Ack:
	  If me.State = Wait_Del_Ack
	  Then
	    me.State := Normal;
	    me.Next := Net[msg_id].Destination;
	    me.IsInList := false;
	    MultiSetRemove(msg_id, Net);
	  Else
	    Error "Got unexpected Del_Ack";
	  Endif;

	Else
	  -- nothing
	Endswitch;
      Endalias;
    Endrule;
  Endchoose;


  Startstate
    For c:CellPtr Do
      Cells[c].Next := c;
      Cells[c].State := Normal;
      Cells[c].IsInList := false;
    Endfor;
    Cells[HeadCell].IsInList := true;
    Undefine Net;
    MultiSetRemovePred(i:Net, true);
    -- start the pred token circulating.
    Send_Pred(Cells[HeadCell].Next, HeadCell);
  End;

  Invariant  "Prev is in list"
    Forall c:CellPtr Do Cells[c].State = Normal End ->
    Forall c:CellPtr Do
      Cells[c].IsInList ->
	Exists p:CellPtr Do 
	  Cells[p].Next = c & Cells[p].IsInList &
	  Forall q:CellPtr Do
	    Cells[q].Next = c
	    -> p = q
	  Endforall
	Endexists
    Endforall;

  Invariant "Next is in list"
    Forall c:CellPtr Do Cells[c].State = Normal End ->
    Forall c:CellPtr Do
      Cells[c].IsInList -> 
	 Cells[Cells[c].Next].IsInList
    Endforall;

  Invariant "Dead cells point to themselves"
    Forall c:CellPtr Do
      !Cells[c].IsInList & Cells[c].State = Normal -> Cells[c].Next = c
    Endforall;






/*
 Rel2.8S

  2 remote cells: 
	   (-sym1) 	107 states, 258 rules fired in 1.07s
           (-nosym) (but with multiset reduction)
	   		211 states, 507 rules fired in 1.11s

  3 remote cells: 
	   (-sym1)      1069 states, 3455 rules fired in 5.61s.
           (-nosym) (but with multiset reduction)
		        6228 states, 20046 rules fired in 18.37s.

gamma2.9S
  6 remove cells:	

     -sym3 196548 states, 987835 rules fired in 1049.18s.


Release 2.9S (Sparc 20, cabbage.stanford.edu)

   4 remote cells
        * The size of each state is 504 bits (rounded up to 64 bytes).
     -b * The size of each state is 224 bits (rounded up to 28 bytes).
        13044 states, 53595 rules fired in 23.24s.
     -b 13044 states, 53595 rules fired in 31.14s.
     -c 13044 states, 53595 rules fired in 18.05s.

*/


