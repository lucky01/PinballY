// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
#pragma once
#include <unordered_set>
#include <propvarutil.h>
#include "DiceCoefficient.h"

class ErrorHandler;
class GameListItem;
class GameSystem;

// DOF COM object class
class __declspec(uuid("{a23bfdbc-9a8a-46c0-8672-60f23d54ffb6}")) DirectOutputComObject;

// DOF client wrapper
class DOFClient
{
public:
	DOFClient();
	~DOFClient();

	// global singleton management
	static bool Init(ErrorHandler &eh);
	static void Shutdown();
	static DOFClient *Get() { return inst; }

	// get the DOF version
	const TCHAR *GetDOFVersion() const { return version.c_str(); }
	
	// Set a DOF "named state" value.  Named states are states identified
	// by arbitrary labels.  These labels are reference in the config tool
	// via "$" tags to trigger specific feedback effects when a named state
	// is matched.
	//
	// The config tool uses these named states for two purposes.  One is
	// for events, like "go to next wheel item" ($PBXWheelRight) or "go
	// to previous menu item ($PBXMenuUp).  The other is for table matching,
	// which is done by ROM name.
	//
	// The state names are arbitrary, but we want to trigger the effects
	// defined for PinballX in the default config tool database, so we use
	// the same state names used in the DOF PinballX plugin.  Note that we
	// don't use that plugin at all, though; we talk directly to the DOF
	// COM object.  So we could add our own custom effects in addition if
	// desired, or use a different set of state names entirely if the
	// config tool were to add a separate database entry for us.
	//
	void SetNamedState(const WCHAR *name, int val);

	// Map a table to a DOF ROM name.  This consults the table/ROM mapping
	// list from the active DOF configuration to find the closest match.
	// We match by ROM name and title, using a fuzzy string match to the
	// title so that we find near matches even if they're not exact. 
	// Returns null if we can't find a mapping list item that's at least
	// reasonably close on the fuzzy match.
	const TCHAR *GetRomForTable(const GameListItem *game);

	// Get a ROM based on a title and optional system.  (The system can
	// be null to look up a ROM purely based on title.)
	const TCHAR *GetRomForTitle(const TCHAR *title, const GameSystem *system = nullptr);

protected:
	// global singleton instance
	static DOFClient *inst;

	// IDispatch interface to DOF object
	RefPtr<IDispatch> pDispatch;

	// DOF version number
	TSTRING version;

	// initialize - load the DOF COM object interface
	bool InitInst(ErrorHandler &eh);

	// load the table mapping file
	void LoadTableMap(ErrorHandler &eh);

	// Game title/ROM mappings from the DOF table mappings file.  The DOF
	// PinballX/front-end configuration uses ROM names to trigger table-
	// specific effects when a game is selected in the menu UI, but the
	// menu UI might only know the title of the table.  SwissLizard ran
	// into this issue when writing the PinballX plugin, and his solution
	// was to use a table title/ROM mapping table generated by the Config
	// Tool to look up the ROM based on title.  Now, DOF and the Config
	// Tool don't actually care about the ROMs qua ROMs; what we're
	// really doing here is coming up with a unique ID for each table 
	// that DOF and the menu system can agree upon, based upon the human-
	// readable table title.  
	//
	// The snag in this approach is that the human-readable titles in 
	// the front-end table list are also human-written, so there can be
	// some superficial variation in the exact rendering of the names,
	// of the sorts that common occur in human-generated text: changes
	// in capitalization, article elisions, misspellings, etc.  The PBX
	// plugin deals with this by fuzzy matching.  It would have been
	// better for DOF to have exposed this as a common service for more
	// uniform behavior, but it doesn't, so we have to replicate the
	// behavior here.  
	//
	// Because of the need for fuzzy matching to the DOF mapping table,
	// we store the mapping table as a simple list of title/ROM pairs.
	// There are ways to index fuzzy-matched data more efficiently than
	// a linear search, but we have a small data set, so I don't think
	// it's worth the trouble.
	//
	// The simplifiedTitle is the title after running it through the
	// SimplifyTitle() function.  This removes extra spaces and
	// punctuation to make fuzzy matching easier.
	//
	struct TitleRomPair
	{
		TitleRomPair(const TCHAR *title, const TCHAR *rom)
			: title(title), rom(rom)
		{
			// pre-compute the bigram set for the simplified title string
			DiceCoefficient::BuildBigramSet(titleBigrams, SimplifiedTitle(title).c_str());
		}

		TSTRING title;
		DiceCoefficient::BigramSet<TCHAR> titleBigrams;
		TSTRING rom;
	};
	std::list<TitleRomPair> titleRomList;

	// Simplified title string.  This removes punctuation marks and
	// collapses runs of whitespace to single spaces.
	static TSTRING SimplifiedTitle(const TCHAR *title);
	
	// Previously resolved mappings.  Whenever we resolve a game's ROM,
	// we'll add an entry here so that we can look up the same game quickly
	// next time it's selected.
	std::unordered_map<const GameListItem*, TSTRING> resolvedRoms;

	// ROM names in the loaded DOF configuration.  This lets us determine
	// if a ROM name from the table database is known in the congiguration,
	// meaning that it will properly trigger table-specific effects if
	// set as the current table.  When a table database entry specifies
	// a ROM, but that ROM isn't in the loaded config, it's better to try
	// to match the table to DOF effects based on the table title.  The
	// reason is that some tables have multiple ROMs available, but the
	// DOF config tool always generates the .ini files for one ROM for
	// each table.  That means the ROM designated in the local table
	// database might be perfectly valid but still not matchable in the
	// DOF config, so we're better off trying to find the one actually
	// used in the config by matching on the game title.
	//
	// This is stored with the lower-case version of the name as the
	// key, and the exact-case version as the value.  This allows quick
	// lookup of the name without regard to case, and retrieves the
	// corresponding exact name that will match the DOF configuration.
	std::unordered_map<TSTRING, TSTRING> knownROMs;


	//
	// DISPIDs for the dispatch functions we need to import
	//

	// void Init(string hostAppName, string tableFileName = "", string gameName = "")
	DISPID dispidInit;

	// void Finish()
	DISPID dispidFinish;

	// string GetVersion()
	DISPID dispidGetVersion;

	// void UpdateTableElement(string elementType, int eleNumber, int value)
	DISPID dispidUpdateTableElement;

	// void UpdateNamedTableElement(string name, int value)
	DISPID dispidUpdateNamedTableElement;

	// String TableMappingFileName()
	DISPID dispidTableMappingFileName;

	// String[] GetConfiguredTableElmentDescriptors() [sic - "Elment" not "Element"]
	DISPID dispidGetConfiguredTableElmentDescriptors;

	
	// EXCEPINFO subclass with auto initialization and cleanup
	struct EXCEPINFOEx : EXCEPINFO
	{
		EXCEPINFOEx() { ZeroMemory(this, sizeof(EXCEPINFO)); }
		~EXCEPINFOEx() { Clear(); }

		void Clear()
		{
			if (bstrDescription != nullptr) SysFreeString(bstrDescription);
			if (bstrSource != nullptr) SysFreeString(bstrSource);
			if (bstrHelpFile != nullptr) SysFreeString(bstrHelpFile);
			ZeroMemory(this, sizeof(EXCEPINFO));
		}
	};

	// VARIANT subclass with auto initialization and cleanup
	struct VARIANTEx : VARIANT
	{
		VARIANTEx() 
		{
			ZeroMemory(this, sizeof(VARIANT));
			vt = VT_NULL;
		}

		~VARIANTEx() { Clear(); }
		
		void Clear() { ClearVariantArray(this, 1); }
	};
};

