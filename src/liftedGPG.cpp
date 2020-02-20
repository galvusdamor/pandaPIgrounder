#include "liftedGPG.h"

#include "gpg.h"

void assignGroundNosToDeleteEffects(const Domain & domain, std::vector<GpgPlanningGraph::ResultType> & groundedTasksPg,std::set<GpgPlanningGraph::StateType> & reachableFacts){
	for (GpgPlanningGraph::ResultType & groundedTask : groundedTasksPg){
		// assign fact NOs to delete effects
		for (const PredicateWithArguments & delEffect : domain.tasks[groundedTask.taskNo].effectsDel){
			GpgPlanningGraph::StateType delState;
			delState.setHeadNo (delEffect.getHeadNo ());
			for (int varIdx : delEffect.arguments)
			{
				delState.arguments.push_back (groundedTask.arguments[varIdx]);
			}

			// Check if we already know this fact
			std::set<GpgPlanningGraph::StateType>::iterator factIt;
			if ((factIt = reachableFacts.find (delState)) != reachableFacts.end())
			{
				// If this delete effect occurs in the list of reachable facts, then add it to the effect list. If not, it can never be true 
				groundedTask.groundedDelEffects.push_back(factIt->groundedNo);
			}
		
		}
	}
}


std::tuple<std::vector<Fact>, std::vector<GroundedTask>, std::vector<GroundedMethod>> run_lifted_HTN_GPG(const Domain & domain, const Problem & problem, 
		bool enableHierarchyTyping, 
		bool futureCachingByPrecondition,
		bool printTimings,
		bool quietMode){
	std::unique_ptr<HierarchyTyping> hierarchyTyping;
	// don't do hierarchy typing for classical instances
	if (problem.initialAbstractTask != -1 && enableHierarchyTyping)
		hierarchyTyping = std::make_unique<HierarchyTyping> (domain, problem);

	if (!quietMode) std::cerr << "Running PG." << std::endl;
	GpgPlanningGraph pg (domain, problem);
	std::vector<GpgPlanningGraph::ResultType> groundedTasksPg;
	std::set<Fact> reachableFacts;
	runGpg (pg, groundedTasksPg, reachableFacts, hierarchyTyping.get (), futureCachingByPrecondition, printTimings, quietMode);
	
	if (!quietMode) std::cerr << "PG done. Postprocessing" << std::endl;
	assignGroundNosToDeleteEffects(domain, groundedTasksPg, reachableFacts);
	validateGroundedList (groundedTasksPg);

	if (!quietMode) std::cerr << "PG postprocessing done." << std::endl;
	if (!quietMode) std::cerr << "Calculated [" << groundedTasksPg.size () << "] grounded tasks and [" << reachableFacts.size () << "] reachable facts." << std::endl;

	if (problem.initialAbstractTask == -1){
		// create facts in the correct order
		std::vector<Fact> reachableFactsList  (reachableFacts.size ());
		for (const Fact & fact : reachableFacts)
			reachableFactsList[fact.groundedNo] = fact;
	
		std::vector<GroundedMethod> no_methods;

		return std::make_tuple(reachableFactsList, groundedTasksPg, no_methods);
	}

	DEBUG(std::cerr << "After lifted PG:" << std::endl;
	for (size_t taskIdx = 0; taskIdx < groundedTasksPg.size (); ++taskIdx)
	{
		const GroundedTask & task = groundedTasksPg[taskIdx];
		assert (task.taskNo < domain.nPrimitiveTasks);
		assert (task.groundedPreconditions.size () == domain.tasks[task.taskNo].preconditions.size ());
		assert (task.groundedDecompositionMethods.size () == 0);
		std::cerr << "    Task " << taskIdx << " (" << task.groundedNo << ", " << ((task.taskNo < domain.nPrimitiveTasks) ? "primitive" : " abstract") << "): " << task.groundedPreconditions.size () << " grounded preconditions (vs " << domain.tasks[task.taskNo].preconditions.size () << "), "
			<< task.groundedDecompositionMethods.size () << " grounded decomposition methods (vs " << domain.tasks[task.taskNo].decompositionMethods.size () << ")." << std::endl;
	});


	DEBUG (
	for (const auto & fact : reachableFacts)
	{
		std::cerr << "Grounded fact #" << fact.groundedNo << " (" << domain.predicates[fact.predicateNo].name << ")" << std::endl;
		std::cerr << std::endl;
	}
	);

	std::vector<int> groundedTasksByTask (domain.nTotalTasks);
	for (const GroundedTask & task : groundedTasksPg)
		++groundedTasksByTask[task.taskNo];

	for (const auto & _method : domain.decompositionMethods)
	{
		DecompositionMethod & method = const_cast<DecompositionMethod &> (_method);
		std::vector<std::pair<size_t, int>> subtasksWithFrequency;
		for (size_t subtaskIdx = 0; subtaskIdx < method.subtasks.size (); ++subtaskIdx)
		{
			const TaskWithArguments & subtask = method.subtasks[subtaskIdx];
			//subtasksWithFrequency.push_back (std::make_pair (groundedTasksByTask[subtask.taskNo], subtaskIdx));
			subtasksWithFrequency.push_back (std::make_pair (domain.tasks[subtask.taskNo].variableSorts.size(), subtaskIdx));
		}
		std::sort (subtasksWithFrequency.begin (), subtasksWithFrequency.end (), std::greater<std::pair<size_t, int>> ());
		std::vector<TaskWithArguments> subtasks (method.subtasks.size ());
		for (size_t subtaskIdx = 0; subtaskIdx < method.subtasks.size (); ++subtaskIdx)
		{
			const auto & foo = subtasksWithFrequency[subtaskIdx];
			subtasks[subtaskIdx] = method.subtasks[foo.second];
		}
		method.subtasks = subtasks;
		std::vector<std::pair<int,int>> newOrdering;
		for (auto & ord : method.orderingConstraints){
			const auto & before = subtasksWithFrequency[ord.first];
			const auto & after = subtasksWithFrequency[ord.second];
			newOrdering.push_back(std::make_pair(before.second, after.second));
		}
		method.orderingConstraints = newOrdering;
	}

	if (!quietMode) std::cerr << "Running TDG." << std::endl;
	GpgTdg tdg (domain, problem, groundedTasksPg);
	std::vector<GpgTdg::ResultType> groundedMethods;
	std::set<GpgTdg::StateType> groundedTaskSetTdg;
	runGpg (tdg, groundedMethods, groundedTaskSetTdg, hierarchyTyping.get (), futureCachingByPrecondition, printTimings, quietMode);
	if (!quietMode) std::cerr << "TDG done." << std::endl;
	if (!quietMode) std::cerr << "Calculated [" << groundedTaskSetTdg.size () << "] grounded tasks and [" << groundedMethods.size () << "] grounded decomposition methods." << std::endl;

	validateGroundedList (groundedMethods);

	// Order grounded tasks correctly
	std::vector<GroundedTask> groundedTasksTdg (groundedTaskSetTdg.size ());
	for (const auto & task : groundedTaskSetTdg)
		groundedTasksTdg[task.groundedNo] = task;

	// Add grounded decomposition methods to the abstract tasks
	for (const GroundedMethod & method : groundedMethods)
		for (auto abstractGroundedTaskNo : method.groundedAddEffects)
			groundedTasksTdg[abstractGroundedTaskNo].groundedDecompositionMethods.push_back (method.groundedNo);

	validateGroundedList (groundedTasksTdg);

	DEBUG (
	for (const auto & task : groundedTasksTdg)
	{
		std::cerr << "Grounded task #" << task.groundedNo << " (" << domain.tasks[task.taskNo].name << ")" << std::endl;
		std::cerr << "Grounded decomposition methods:";
		for (const auto & prec : task.groundedDecompositionMethods)
			std::cerr << " " << prec;
		std::cerr << std::endl;
		std::cerr << "Grounded preconditions:";
		for (const auto & prec : task.groundedPreconditions)
			std::cerr << " " << prec;
		std::cerr << std::endl;
		std::cerr << "Grounded add effects:";
		for (const auto & prec : task.groundedAddEffects)
			std::cerr << " " << prec;
		std::cerr << std::endl;
		std::cerr << std::endl;
	}

	for (const auto & method : groundedMethods)
	{
		std::cerr << "Grounded method #" << method.groundedNo << " (" << domain.decompositionMethods[method.methodNo].name << ")" << std::endl;
		std::cerr << "Grounded preconditions:";
		for (const auto & prec : method.groundedPreconditions)
			std::cerr << " " << prec << " (" << domain.tasks[groundedTasksTdg[prec].taskNo].name << ")";
		std::cerr << std::endl;
		std::cerr << "Grounded add effects:";
		for (const auto & prec : method.groundedAddEffects)
			std::cerr << " " << prec << " (" << domain.tasks[groundedTasksTdg[prec].taskNo].name << ")";
		std::cerr << std::endl;
		std::cerr << std::endl;
	}
	);

	// Perform DFS
	if (!quietMode) std::cerr << "Performing DFS." << std::endl;
	std::vector<GroundedTask> reachableTasksDfs;
	std::vector<GroundedMethod> reachableMethodsDfs;
	tdgDfs (reachableTasksDfs, reachableMethodsDfs, groundedTasksTdg, groundedMethods, domain, problem);
	if (!quietMode) std::cerr << "DFS done." << std::endl;
	if (!quietMode) std::cerr << "After DFS: " << reachableTasksDfs.size () << " tasks, " << reachableMethodsDfs.size () << " methods." << std::endl;

	DEBUG(size_t tmp = 0;
	for (const auto & t : reachableTasksDfs)
		if (t.taskNo < domain.nPrimitiveTasks)
			++tmp;
	std::cerr << "Primitive: " << tmp << std::endl;);


	// create facts in the correct order
	std::vector<Fact> reachableFactsList  (reachableFacts.size ());
	for (const Fact & fact : reachableFacts)
		reachableFactsList[fact.groundedNo] = fact;

	// validate all lists
	validateGroundedList (reachableTasksDfs);
	validateGroundedList (reachableMethodsDfs);
	validateGroundedList (reachableFactsList);


	return std::make_tuple(reachableFactsList, reachableTasksDfs, reachableMethodsDfs);
}